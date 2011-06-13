/* 
 * Copyright (C) 2011 RobotCub Consortium, European Commission FP6 Project IST-004370
 * Author: Ugo Pattacini
 * email:  ugo.pattacini@iit.it
 * website: www.robotcub.org
 * Permission is granted to copy, distribute, and/or modify this program
 * under the terms of the GNU General Public License, version 2 or any
 * later version published by the Free Software Foundation.
 *
 * A copy of the license can be found at
 * http://www.robotcub.org/icub/license/gpl.txt
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details
*/

#include <assert.h>

#include <yarp/os/Network.h>

#include <iCub/ctrl/math.h>
#include <iCub/perception/springyFingers.h>

using namespace std;
using namespace yarp::os;
using namespace yarp::sig;
using namespace iCub::ctrl;
using namespace iCub::learningmachine;
using namespace iCub::perception;


/************************************************************************/
SpringyFinger::SpringyFinger()
{
    threshold=0.0;
}


/************************************************************************/
bool SpringyFinger::fromProperty(const Property &options)
{
    lssvm.reset();

    Property &opt=const_cast<Property&>(options);
    assert(opt.check("name"));
    name=opt.find("name").asString().c_str();
    threshold=opt.check("thres",Value(0.0)).asDouble();

    scaler.setLowerBoundIn(0.0);
    scaler.setUpperBoundIn(360.0);
    lssvm.setDomainSize(1);

    if ((name=="thumb") || (name=="index") || (name=="middle"))
        lssvm.setCoDomainSize(2);
    else if ((name=="ring") || (name=="little"))
        lssvm.setCoDomainSize(3);
    else
        return false;

    if (opt.check("scaler"))
        scaler.fromString(opt.find("scaler").asString().c_str());

    if (opt.check("lssvm"))
        lssvm.fromString(opt.find("lssvm").asString().c_str());

    return true;
}


/************************************************************************/
void SpringyFinger::toProperty(Property &options) const
{
    options.put("name",name.c_str());
    options.put("thres",threshold);
    options.put("scaler",scaler.toString().c_str());
    options.put("lssvm",lssvm.toString().c_str());
}


/************************************************************************/
bool SpringyFinger::getData(Vector &in, Vector &out) const
{
    map<string,Sensor*>::const_iterator In_0=sensors.find("In_0");
    map<string,Sensor*>::const_iterator Out_0=sensors.find("Out_0");
    map<string,Sensor*>::const_iterator Out_1=sensors.find("Out_1");
    map<string,Sensor*>::const_iterator Out_2=sensors.find("Out_2");

    bool ok=true;
    ok&=In_0!=sensors.end();
    ok&=Out_0!=sensors.end();
    ok&=Out_1!=sensors.end();

    if (lssvm.getCoDomainSize()>2)
        ok&=Out_2!=sensors.end();

    if (!ok)
        return false;

    Value val_in, val_out(3);
    In_0->second->getInput(val_in);
    Out_0->second->getInput(val_out[0]);
    Out_1->second->getInput(val_out[1]);

    in.resize(lssvm.getDomainSize());
    in[0]=val_in.asDouble();

    out.resize(lssvm.getCoDomainSize());
    out[0]=val_out[0].asDouble();
    out[1]=val_out[1].asDouble();

    if (lssvm.getCoDomainSize()>2)
    {
        Out_2->second->getInput(val_out[2]);
        out[2]=val_out[2].asDouble();
    }

    return true;
}


/************************************************************************/
bool SpringyFinger::getOutput(Value &out) const
{
    Vector i,o;
    if (!getData(i,o))
        return false;

    i[0]=scaler.transform(i[0].asDouble());
    Vector pred=lssvm.predict(i);

    for (int j=0; j<pred.length(); j++)
        pred[j]=scaler.unTransform(pred[j]);

    Property prop;
    prop.put("prediction",Value(pred.toString().c_str()));
    prop.put("out",Value(norm(o-pred)>thres?1:0));

    out=Value(prop.toString().c_str());

    return true;
}


/************************************************************************/
bool SpringyFinger::calibrate(const Property &options)
{
    Property &opt=const_cast<Property&>(options);

    if (opt.check("reset"))
        lssvm.reset();

    if (opt.check("feed"))
    {
        Vector in,out;
        if (getData(in,out))
        {
            in[0]=scaler.transform(in[0]);
            for (int i=0; i<out.length(); i++)
                out[i]=scaler.transform(out[i]);

            lssvm.feedSample(in,out);
        }
        else
            return false;
    }

    if (opt.check("train"))
        lssvm.train();

    return true;
}


/************************************************************************/
SpringyFingersModel::SpringyFingersModel()
{
    configured=false;
}


/************************************************************************/
bool SpringyFingersModel::fromProperty(const Property &options)
{
    if (configured)
        close();

    Property &opt=const_cast<Property&>(options);
    assert(opt.check("name"));
    assert(opt.check("type"));
    name=opt.find("name").asString().c_str();
    string robot=opt.check("robot",Value("icub")).asString().c_str();
    string type=opt.find("type").asString().c_str();

    string part_motor=string("/"+type+"_arm");
    string part_analog=string("/"+type+"_hand");

    Property prop;
    prop.put("robot",robot.c_str());
    prop.put("remote",("/"+robot+part_motor).c_str());
    prop.put("local",("/"+name+part_motor).c_str());
    if (!driver.open(prop))
        return false;

    driver.view(limits);
    driver.view(encoders);

    port.open(("/"+name+part_analog+"/analog:i").c_str());
    if (!Network::connect(port.name.c_str(),("/"+robot+part_analog+"/analog:o").c_str(),"udp"))
    {
        port.close();
        return false;
    }

    // configure interface-based sensors
    Property propGen;
    propThumb.put("name","In_0");
    propGen.put("type","pos");
    propGen.put("size",16);

    Property propThumb=propGen;  propThumb.put("index",10);
    Property propIndex=propGen;  propIndex.put("index",12);
    Property propMiddle=propGen; propMiddle.put("index",14);
    Property propRing=propGen;   propRing.put("index",15);
    Property propLittle=propGen; propLittle.put("index",15);

    sensIF[0].configure(encoders,propThumb);
    sensIF[1].configure(encoders,propIndex);
    sensIF[2].configure(encoders,propMiddle);
    sensIF[3].configure(encoders,propRing);
    sensIF[4].configure(encoders,propLittle);

    // configure port-based sensors
    Property thumb_mp("(name Out_0) (index 1)");
    Property thumb_ip("(name Out_1) (index 2)");
    Property index_mp("(name Out_0) (index 4)");
    Property index_ip("(name Out_1) (index 5)");
    Property middle_mp("(name Out_0) (index 7)");
    Property middle_ip("(name Out_1) (index 8)");
    Property ring_mp("(name Out_0) (index 9)");
    Property ring_pip("(name Out_1) (index 10)");
    Property ring_dip("(name Out_2) (index 11)");
    Property little_mp("(name Out_0) (index 12)");
    Property little_pip("(name Out_1) (index 13)");
    Property little_dip("(name Out_2) (index 14)");

    sensPort[0].configure(&port,thumb_mp);
    sensPort[1].configure(&port,thumb_ip);
    sensPort[2].configure(&port,index_mp);
    sensPort[3].configure(&port,index_ip);
    sensPort[4].configure(&port,middle_mp);
    sensPort[5].configure(&port,middle_ip);
    sensPort[6].configure(&port,ring_mp);
    sensPort[7].configure(&port,ring_pip);
    sensPort[8].configure(&port,ring_dip);
    sensPort[9].configure(&port,little_mp);
    sensPort[10].configure(&port,little_pip);
    sensPort[11].configure(&port,little_dip);

    // configure fingers
    Property thumb(opt.findGroup("thumb").toString().c_str());
    Property index(opt.findGroup("index").toString().c_str());
    Property middle(opt.findGroup("middle").toString().c_str());
    Property ring(opt.findGroup("ring").toString().c_str());
    Property little(opt.findGroup("little").toString().c_str());

    fingers[0].fromProperty(thumb);
    fingers[1].fromProperty(index);
    fingers[2].fromProperty(middle);
    fingers[3].fromProperty(ring);
    fingers[4].fromProperty(little);

    // attach sensors to fingers
    fingers[0].attachSensor(sensIF[0]);
    fingers[0].attachSensor(sensPort[0]);
    fingers[0].attachSensor(sensPort[1]);

    fingers[1].attachSensor(sensIF[1]);
    fingers[1].attachSensor(sensPort[2]);
    fingers[1].attachSensor(sensPort[3]);

    fingers[2].attachSensor(sensIF[2]);
    fingers[2].attachSensor(sensPort[4]);
    fingers[2].attachSensor(sensPort[5]);

    fingers[3].attachSensor(sensIF[3]);
    fingers[3].attachSensor(sensPort[6]);
    fingers[3].attachSensor(sensPort[7]);
    fingers[3].attachSensor(sensPort[8]);

    fingers[4].attachSensor(sensIF[4]);
    fingers[4].attachSensor(sensPort[9]);
    fingers[4].attachSensor(sensPort[10]);
    fingers[4].attachSensor(sensPort[11]);

    return configured=true;
}


/************************************************************************/
void SpringyFingersModel::close()
{
    if (configured)
    {
        driver.close();

        port.interrupt();
        port.close();

        configured=false;
    }
}


/************************************************************************/
SpringyFingersModel::~SpringyFingersModel()
{
    close();
}



