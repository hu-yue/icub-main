/*
 * Copyright (C) 2006-2018 Istituto Italiano di Tecnologia (IIT)
 * All rights reserved.
 * Author: Valentina Gaggero
 * This software may be modified and distributed under the terms of the
 * BSD-3-Clause license. See the accompanying LICENSE file for details.
 */

/**
 * @ingroup icub_hardware_modules 
 * \defgroup analogSensorEth
 *
 * To Do: add description
 *
 */

#ifndef __embObjFTsensor_h__
#define __embObjFTsensor_h__

#include <yarp/dev/DeviceDriver.h>
#include <yarp/dev/IAnalogSensor.h>
#include "IethResource.h"


#include "serviceParser.h"


namespace yarp {
    namespace dev {
        class embObjFTsensor;
    }
}


#define EMBOBJSTRAIN_USESERVICEPARSER

// -- class embObjFTsensor

class yarp::dev::embObjFTsensor:    public yarp::dev::IAnalogSensor,
                                    public yarp::dev::DeviceDriver,
                                    public eth::IethResource
{

public:

    enum { strain_Channels = 6, strain_FormatData = 16 };

public:

    embObjFTsensor();
    ~embObjFTsensor();

    // An open function yarp factory compatible
    bool open(yarp::os::Searchable &config);
    bool close();

    // IAnalogSensor interface
    virtual int read(yarp::sig::Vector &out);
    virtual int getState(int ch);
    virtual int getChannels();
    virtual int calibrateChannel(int ch, double v);
    virtual int calibrateSensor();
    virtual int calibrateSensor(const yarp::sig::Vector& value);
    virtual int calibrateChannel(int ch);

    // IethResource interface
    virtual bool initialised();
    virtual eth::iethresType_t type();
    virtual bool update(eOprotID32_t id32, double timestamp, void* rxdata);

private:
    void *mPriv;

    std::vector<double> analogdata;
    std::vector<double> offset;
    std::vector<double> scaleFactor;

    bool scaleFactorIsFilled;
    bool useCalibValues;
    bool useTemperature;
    int lastTemperature;

private:

    // for all
    bool fromConfig(yarp::os::Searchable &config,  servConfigFTsensor_t &serviceConfig);
    bool initRegulars(servConfigFTsensor_t &serviceConfig);
    void cleanup(void);
    void printServiceConfig(servConfigFTsensor_t &serviceConfig);
    std::string getBoardInfo(void) const;
    // for strain
    bool fillScaleFactor(servConfigFTsensor_t &serviceConfig);
    bool sendConfig2Strain(servConfigFTsensor_t &serviceConfig);
    bool updateStrainValues(eOprotID32_t id32, double timestamp, void* rxdata);
    
    //for temeprature
    bool updateTemperatureValues(eOprotID32_t id32, double timestamp, void* rxdata);
    
    // for ??
    void resetCounters();
    void getCounters(unsigned int &saturations, unsigned int &errors, unsigned int &timeouts);
};


#endif

