
// -*- mode:C++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-

/*
* Copyright (C) 2012 Robotcub Consortium
* Author: Valentina Gaggero
* CopyPolicy: Released under the terms of the GNU GPL v2.0.
*
*/

// general purpose stuff.
#include <string>
#include <iostream>
#include <string.h>

// Yarp Includes
#include <yarp/os/Time.h>
#include <yarp/os/Semaphore.h>


// specific to this device driver.
#include <embObjFTsensor.h>

#include "EoAnalogSensors.h"
#include "EOnv_hid.h"
#include "EoProtocolAS.h"
#include "EOconstarray.h"


#ifdef WIN32
#pragma warning(once:4355)
#endif

#include "embObjGeneralDevPrivData.h"


using namespace yarp;
using namespace yarp::os;
using namespace yarp::dev;


class eo_ftsens_privData : public embObjDevPrivData
{
public:
    yarp::os::Semaphore mutex;
    eo_ftsens_privData();
    ~eo_ftsens_privData();
    
};

eo_ftsens_privData::eo_ftsens_privData()
{;}
eo_ftsens_privData::~eo_ftsens_privData()
{;}

#define GET_privData(x) (*((static_cast<eo_ftsens_privData*>(x))))


bool embObjFTsensor::fromConfig(yarp::os::Searchable &_config, servConfigFTsensor_t &serviceConfig)
{
    
    ServiceParser* parser = new ServiceParser;
    bool ret = parser->parseService(_config, serviceConfig);
    delete parser;
    
    useCalibValues = serviceConfig.useCalibration;
    if(serviceConfig.temperatureAcquisitionrate > 0)
        useTemperature = true;
    
    return ret;
}


embObjFTsensor::embObjFTsensor()
{
    mPriv = new eo_ftsens_privData();
    useCalibValues = false;
    useTemperature = false;
    analogdata.resize(0);
    offset.resize(0);
    scaleFactor.resize(0);
    scaleFactorIsFilled = false;
}


embObjFTsensor::~embObjFTsensor()
{
    analogdata.resize(0);
    offset.resize(0);
    scaleFactor.resize(0);

}


std::string embObjFTsensor::getBoardInfo(void) const
{
    return GET_privData(mPriv).getBoardInfo();
}

bool embObjFTsensor::initialised()
{
    return GET_privData(mPriv).isOpen();
}


bool embObjFTsensor::open(yarp::os::Searchable &config)
{
    // - first thing to do is verify if the eth manager is available. then i parse info about the eth board.

    GET_privData(mPriv).ethManager = eth::TheEthManager::instance();
    if(NULL == GET_privData(mPriv).ethManager)
    {
        yFatal() << "embObjFTsensor::open() fails to instantiate ethManager";
        return false;
    }

    string boardIPstring;
    string boardName;
    eOipv4addr_t ipv4addr;
    

    if(false == GET_privData(mPriv).ethManager->verifyEthBoardInfo(config, ipv4addr, boardIPstring, boardName))
    {
        yError() << "embObjFTsensor::open(): object TheEthManager fails in parsing ETH propertiex from xml file";
        return false;
    }
    // add specific info about this device ...

    // when we split this device in three ones ... _as_type will be lost and we can move code tagged with tag__XXX0123_ in here


    // - now all other things

    // read stuff from config file
    servConfigFTsensor_t serviceConfig;
    if(!fromConfig(config, serviceConfig))
    {
        yError() << "embObjFTsensor for BOARD w/ IP" << boardIPstring << "is missing some configuration parameter. Check logs and your config file.";
        return false;
    }

    // and prepare analogdata + scaleFactor
    {
        // they must be of size: strain_Channels, and 0.0-initted / 1.0-initted
        analogdata.resize(strain_Channels, 0.0);
        offset.resize(strain_Channels, 0.0);
        scaleFactor.resize(strain_Channels, 1.0);
    }


    // some other things ... tag__XXX0123_



    // -- instantiate EthResource etc.

    GET_privData(mPriv).res = GET_privData(mPriv).ethManager->requestResource2(this, config);
    if(NULL == GET_privData(mPriv).res)
    {
        yError() << "embObjFTsensor::open() fails because could not instantiate the ethResource for BOARD w/ IP = " << boardIPstring << " ... unable to continue";
        return false;
    }

    printServiceConfig(serviceConfig);

    if(!GET_privData(mPriv).res->verifyEPprotocol(eoprot_endpoint_analogsensors))
    {
        cleanup();
        return false;
    }


#if defined(EMBOBJSTRAIN_USESERVICEPARSER)
    const eOmn_serv_parameter_t* servparam = &serviceConfig.ethservice;
#else
    const eOmn_serv_parameter_t* servparam = NULL;
#endif

    if(false == GET_privData(mPriv).res->serviceVerifyActivate(eomn_serv_category_strain, servparam, 5.0))
    {
        yError() << "embObjFTsensor::open() has an error in call of ethResources::serviceVerifyActivate() for BOARD" << GET_privData(mPriv).getBoardInfo();
        printServiceConfig(serviceConfig);
        cleanup();
        return false;
    }


    printServiceConfig(serviceConfig);

    // we always prepare the fullscales.
    if(false == fillScaleFactor(serviceConfig))
    {
        yError() << "embObjFTsensor::open() has failed in calling  embObjFTsensor::fillScaleFactor()";
        return false;
    }

    if(false == sendConfig2Strain(serviceConfig))
    {
        cleanup();
        return false;
    }

    if(false == initRegulars(serviceConfig))
    {
        cleanup();
        return false;
    }


    if(false == GET_privData(mPriv).res->serviceStart(eomn_serv_category_strain))
    {
        yError() << "embObjFTsensor::open() fails to start service for BOARD" << GET_privData(mPriv).getBoardInfo();
        cleanup();
        return false;
    }
    else
    {
        if(GET_privData(mPriv).isVerbose())
        {
            yDebug() << "embObjFTsensor::open() correctly starts as service of BOARD" << GET_privData(mPriv).getBoardInfo();
        }
    }

    GET_privData(mPriv).setOpen(true);
    return true;
}


bool embObjFTsensor::sendConfig2Strain(servConfigFTsensor_t &serviceConfig)
{
    eOas_strain_config_t strainConfig = {0};

    strainConfig.datarate = serviceConfig.acquisitionrate;
    strainConfig.signaloncefullscale = eobool_false;
    strainConfig.mode = (true == serviceConfig.useCalibration) ? (eoas_strainmode_txcalibrateddatacontinuously) : (eoas_strainmode_txuncalibrateddatacontinuously);

    // version with read-back

    eOprotID32_t id32 = eoprot_ID_get(eoprot_endpoint_analogsensors, eoprot_entity_as_strain, 0, eoprot_tag_as_strain_config);

    if(false == GET_privData(mPriv).res->setcheckRemoteValue(id32, &strainConfig, 10, 0.010, 0.050))
    {
        yError() << "FATAL: embObjFTsensor::sendConfig2Strain() had an error while calling setcheckRemoteValue() for strain config in BOARD" << GET_privData(mPriv).getBoardInfo();
        return false;
    }
    else
    {
        if(GET_privData(mPriv).isVerbose())
        {
            yDebug() << "embObjFTsensor::sendConfig2Strain() correctly configured strain coinfig in BOARD" << GET_privData(mPriv).getBoardInfo();
        }
    }
    
    //configure the service of temperature 
    id32 = eoprot_ID_get(eoprot_endpoint_analogsensors, eoprot_entity_as_temperature, 0, eoprot_tag_as_temperature_config);
    
    
    eOas_temperature_config_t tempconfig = {0};
    if(serviceConfig.temperatureAcquisitionrate > 0)
    {
        tempconfig.enabled = 1;
        tempconfig.datarate = serviceConfig.temperatureAcquisitionrate;
    }

    return true;
}



//#warning --> marco.accame: review function embObjFTsensor::fillScaleFactor() as in comment below
// it is better to change the behaviour of the function so that: 1. we send the request, 2. we wait for the sig<> and unblock a mutex
// current implementation relies on a wait of 1 sec and check of non-zero length of an array: smart but not the rigth way to do it.

// EVEN better: in serviceVerifyActivate() we allow the retrieval of a parameter which the ETH board sends back. in such a param it is contained
// the fullscales values ...

// marco.accame on 09 jan 2018: much better using an ask(id32_fullscale) and making this variable proxied inside the ETH board ...

bool embObjFTsensor::fillScaleFactor(servConfigFTsensor_t &serviceConfig)
{
    // if we already have set the scalefactor ...
    if(true == scaleFactorIsFilled)
    {
        return true;
    }

    // at first we set the scale factors to 1, so that we are sure they have a safe value. it redundant, as we already set it to 1.0
    for(size_t i = 0; i<scaleFactor.size(); i++)
    {
        scaleFactor[i] = 1.0f;
    }

    // if we dont need calibration we are done
    if(false == useCalibValues)
    {
        if(GET_privData(mPriv).isVerbose())
        {
            yDebug() << "embObjFTsensor::fillScaleFactor(): we DONT use calibration, thus all scale factors are set to 1.0";
        }

        scaleFactorIsFilled = true;
        return true;
    }

    // if we need calibration, then we need to ask the fullscales directly to the strain


    // marco.accame on 11 apr 2014:
    // added the code under ifdef 1. the reason is that one cannot rely on validity of data structures inside the EOnv, as in protocol v2 the requirement is that
    // the initialisation is not specialised and is ... all zeros. if the user wants to init to proper values must redefine the relevant INIT funtion.
    // in this case, the eoprot_fun_INIT_as_strain_status_fullscale().
    // extern void eoprot_fun_INIT_as_strain_status_fullscale(const EOnv* nv)
    // {
    //     eOas_arrayofupto12bytes_t fullscale_values = {0};
    //     eo_array_New(6, 2, &fullscale_values); // itemsize = 2, capacity = 6
    //     eo_nv_Set(nv, &fullscale_values, eobool_true, eo_nv_upd_dontdo);    
    // }
    // moreover, even if properly initted, it is required to set the size to 0 because the size being not 0 is the check of reception of a message.


    bool gotFullScaleValues = false;


    // Check initial size of array...  it should be zero.
    int timeout, NVsize;
    EOnv tmpNV;
    EOnv *p_tmpNV = NULL;
    eOas_arrayofupto12bytes_t fullscale_values = {0};
    // force it to be an empty array of itemsize 2 and capacity 6. 
    // the reason is that the eoprot_tag_as_strain_status_fullscale contains 3 forces and 3 torques each of 2 bytes. see eOas_strain_status_t in EoAnalogSensors.h
    eo_array_New(6, 2, &fullscale_values);

    eOprotID32_t id32_fullscale = eoprot_ID_get(eoprot_endpoint_analogsensors, eoprot_entity_as_strain, 0, eoprot_tag_as_strain_status_fullscale);

        
    // at first we impose that the local value of fullscales is zero.
    // we also force the change because this variable is readonly
    const bool overrideROprotection = true;
    GET_privData(mPriv).res->setLocalValue(id32_fullscale, &fullscale_values, overrideROprotection);

        
    // Prepare analog sensor
    eOas_strain_config_t strainConfig = {0};
    strainConfig.datarate               = serviceConfig.acquisitionrate;
    strainConfig.mode                   = eoas_strainmode_acquirebutdonttx;
    strainConfig.signaloncefullscale    = eobool_true;

    eOprotID32_t id32_strain_config = eoprot_ID_get(eoprot_endpoint_analogsensors, eoprot_entity_as_strain, 0, eoprot_tag_as_strain_config);

    timeout = 5;

    // wait for response
    while(!gotFullScaleValues && (timeout != 0))
    {
        GET_privData(mPriv).res->setRemoteValue(id32_strain_config, &strainConfig);
        SystemClock::delaySystem(1.0);
        // read fullscale values
        GET_privData(mPriv).res->getLocalValue(id32_fullscale, &fullscale_values);
        // If data arrives, size is bigger than zero
        //#warning --> marco.accame says: to wait for 1 sec and read size is ok. a different way is to ... wait for a semaphore incremented by the reply of the board. think of it!
        NVsize = eo_array_Size((EOarray *)&fullscale_values);

        if(0 != NVsize)
        {
            gotFullScaleValues = true;
            break;
        }

        timeout--;
        if(GET_privData(mPriv).isVerbose())
        {
            yWarning() << "embObjFTsensor::fillScaleFactor(): for  BOARD" << GET_privData(mPriv).getBoardInfo();
        }
    }

    if((false == gotFullScaleValues) && (0 == timeout))
    {
        yError() << "embObjFTsensor::fillScaleFactor(): ETH Analog sensor: request for calibration parameters timed out for  BOARD" << GET_privData(mPriv).getBoardInfo();
        return false;
    }

    if((strain_Channels != NVsize))
    {
        yError() << "Analog sensor Calibration data has a different size from channels number in configuration file for  BOARD" << GET_privData(mPriv).getBoardInfo();
        return false;
    }


    if(gotFullScaleValues)
    {
        if(GET_privData(mPriv).isVerbose())
        {
            yWarning() << "embObjFTsensor::fillScaleFactor() detected that already has full scale values for BOARD" << GET_privData(mPriv).getBoardInfo();
            yDebug()   << "embObjFTsensor::fillScaleFactor(): Fullscale values for BOARD" << GET_privData(mPriv).getBoardInfo() << "are: size=" <<  eo_array_Size((EOarray *)&fullscale_values) << "  numchannel=" <<  strain_Channels;
        }

        for (size_t i = 0; i<scaleFactor.size(); i++)
        {
            // Get the k-th element of the array as a 2 bytes msg
            uint8_t *msg = (uint8_t *) eo_array_At((EOarray *) &fullscale_values, i);
            if(NULL == msg)
            {
                yError() << "I don't receive data for channel " << i;
                return false;
            }
            // Got from CanBusMotionControl... here order of bytes seems inverted with respect to calibratedValues or uncalibratedValues (see callback of can strain messages inside the FW of ETHBOARD)
            scaleFactor[i] = ((uint16_t)(msg[0]<<8) | msg[1]);
            //yError() << " scale factor[" << i << "] = " << scaleFactor[i];
            if(GET_privData(mPriv).isVerbose())
            {
                yDebug() << "embObjFTsensor::fillScaleFactor(): channel " << i << "full scale value " << scaleFactor[i];
            }
        }

        scaleFactorIsFilled = true;
    }

    return scaleFactorIsFilled;
}


bool embObjFTsensor::initRegulars(servConfigFTsensor_t &serviceConfig)
{
    // configure regular rops

    vector<eOprotID32_t> id32v(0);
    eOprotID32_t id32 = eo_prot_ID32dummy;

    // we need to choose the id32 to put inside the vector

    if(true == serviceConfig.useCalibration)
    {
        id32 = eoprot_ID_get(eoprot_endpoint_analogsensors, eoprot_entity_as_strain, 0, eoprot_tag_as_strain_status_calibratedvalues);
    }
    else
    {
        id32 = eoprot_ID_get(eoprot_endpoint_analogsensors, eoprot_entity_as_strain, 0, eoprot_tag_as_strain_status_uncalibratedvalues);
    }


    // put it inside vector

    id32v.push_back(id32);

    //Configure to send temperature status
    id32 = eoprot_ID_get(eoprot_endpoint_analogsensors, eoprot_entity_as_temperature, 0, eoprot_tag_as_temperature_status);
    id32v.push_back(id32);
    
    // now we send the vector

    if(false == GET_privData(mPriv).res->serviceSetRegulars(eomn_serv_category_strain, id32v))
    {
        yError() << "embObjFTsensor::initRegulars() fails to add its variables to regulars: cannot proceed any further";
        return false;
    }
    else
    {
        if(GET_privData(mPriv).isVerbose())
        {
            yDebug() << "embObjFTsensor::initRegulars() added" << id32v.size() << "regular rops to BOARD" << GET_privData(mPriv).getBoardInfo();
            char nvinfo[128];
            for (size_t r = 0; r<id32v.size(); r++)
            {
                uint32_t item = id32v.at(r);
                eoprot_ID2information(item, nvinfo, sizeof(nvinfo));
                yDebug() << "\t it added regular rop for" << nvinfo;
            }
        }
    }
   
    return true;
}


/*! Read a vector from the sensor.
 * @param out a vector containing the sensor's last readings.
 * @return AS_OK or return code. AS_TIMEOUT if the sensor timed-out.
 **/
int embObjFTsensor::read(yarp::sig::Vector &out)
{
    // This method gives analogdata to the analogServer

    if(false == GET_privData(mPriv).isOpen())
    {
        return false;
    }

    GET_privData(mPriv).mutex.wait();

    out.resize(analogdata.size());
    for (size_t k = 0; k<analogdata.size(); k++)
    {
        out[k] = analogdata[k]+offset[k];
    }


    GET_privData(mPriv).mutex.post();
    
    return IAnalogSensor::AS_OK;;
}




int embObjFTsensor::getState(int ch)
{
    printf("getstate\n");
    return AS_OK;
}


int embObjFTsensor::getChannels()
{
    return strain_Channels;
}


int embObjFTsensor::calibrateSensor()
{
    GET_privData(mPriv).mutex.wait();
    for (size_t i = 0; i < analogdata.size(); i++)
    {
        offset[i] = -analogdata[i];
    }
    GET_privData(mPriv).mutex.post();
    return AS_OK;
}


int embObjFTsensor::calibrateSensor(const yarp::sig::Vector& value)
{
    return AS_OK;
}


int embObjFTsensor::calibrateChannel(int ch)
{
    return AS_OK;
}


int embObjFTsensor::calibrateChannel(int ch, double v)
{
    return AS_OK;
}

eth::iethresType_t embObjFTsensor::type()
{
    return eth::iethres_analogstrain;
}

bool embObjFTsensor::update(eOprotID32_t id32, double timestamp, void* rxdata)
{
    bool ret = false;

    if(false == GET_privData(mPriv).isOpen())
    {
        return ret;;
    }
    eOprotEntity_t entity = eoprot_ID2entity(id32); 
    
    switch(entity)
    {
        case eoas_entity_strain:
        {
            ret = updateStrainValues(id32, timestamp, rxdata);
        }break;
        
        case eoas_entity_temperature:
        {
            ret = updateTemperatureValues(id32, timestamp, rxdata);
        }break;
        default:
        {
            ret = false;
            yError() << "embObjFTsensor::update failed";
        }
    };
    return ret;
}

bool embObjFTsensor::updateStrainValues(eOprotID32_t id32, double timestamp, void* rxdata)
{
    id32 = id32;
    timestamp = timestamp;
    
   
    // called by feat_manage_analogsensors_data() which is called by:
    // eoprot_fun_UPDT_as_strain_status_calibratedvalues() or eoprot_fun_UPDT_as_strain_status_uncalibratedvalues()
    // the void* parameter inside this function is a eOas_arrayofupto12bytes_t*
    // and can be treated as a EOarray
    
    EOarray *array = (EOarray*)rxdata;
    uint8_t size = eo_array_Size(array);
    uint8_t itemsize = eo_array_ItemSize(array); // marco.accame: must be 2, as the code after uses this convention
    if((0 == size) || (2 != itemsize))
    {
        return false;
    }
    
    // lock analogdata
    GET_privData(mPriv).mutex.wait();
    
    for (size_t k = 0; k<analogdata.size(); k++)
    {
        // Get the kth element of the array as a 2 bytes msg
        char* tmp = (char*) eo_array_At(array, k);
        // marco.accame: i am nervous about iterating for strain_Channels instead of size of array....
        //               thus i add a protection. if k goes beyond size of array, eo_array_At() returns NULL.
        if(NULL != tmp)
        {
            uint8_t msg[2] = {0};
            memcpy(msg, tmp, 2);
            // Got from canBusMotionControl
            analogdata[k] = (short)( ( (((unsigned short)(msg[1]))<<8)+msg[0]) - (unsigned short) (0x8000) );
            
            if(true == useCalibValues)
            {
                analogdata[k] = analogdata[k]*scaleFactor[k]/float(0x8000);
            }
        }
    }
    
    // unlock analogdata
    GET_privData(mPriv).mutex.post();

    return true;
}

bool embObjFTsensor::updateTemperatureValues(eOprotID32_t id32, double timestamp, void* rxdata)
{
    eOas_temperature_status_t *temp_st = (eOas_temperature_status_t *)rxdata;
    
    EOconstarray* arrayofvalues = eo_constarray_Load(reinterpret_cast<const EOarray*>(&temp_st->arrayofdata));
    
    uint8_t numofIntem2update = eo_constarray_Size(arrayofvalues);
    
    if(numofIntem2update>1)
        yError() << "embObjFTStrain" << getBoardInfo() << "update(): I expect 1 item, but I received " << numofIntem2update ;
    
    for(int i=0; i<numofIntem2update; i++)
    {
        eOas_temperature_data_t *data = (eOas_temperature_data_t*) eo_constarray_At(arrayofvalues, i);
        if(data == NULL)
        {
            yError() << "embObjFTStrain" << getBoardInfo() << "update(): I have to update " << numofIntem2update << "items, but the " << i << "-th item is null.";
            continue; 
            //NOTE: I signal this strange situation with an arror for debug porpouse...maybe we can convert in in warning when the device is stable....
        }
        else
        {
            yDebug() << "embObjFTStrain" << getBoardInfo() << ": val "<< i<< "/" << numofIntem2update << ": value=" << data->value << ", time=" << data->timestamp; 
        }
    }
    
    return true;
}

bool embObjFTsensor::close()
{
    GET_privData(mPriv).setOpen(false);

    cleanup();
    return true;
}


void embObjFTsensor::printServiceConfig(servConfigFTsensor_t &serviceConfig)
{    
    char loc[20] = {0};
    char fir[20] = {0};
    char pro[20] = {0};
    const char * boardname = (NULL != GET_privData(mPriv).res) ? (GET_privData(mPriv).res->getProperties().boardnameString.c_str()) : ("NOT-ASSIGNED-YET");
    const char * ipv4 = (NULL != GET_privData(mPriv).res) ? (GET_privData(mPriv).res->getProperties().ipv4addrString.c_str()) : ("NOT-ASSIGNED-YET");
    const char * boardtype = eoboards_type2string2(static_cast<eObrd_type_t>(serviceConfig.ethservice.configuration.data.as.strain.boardtype.type), eobool_true);
    ServiceParser *parser =  new ServiceParser();
    parser->convert(serviceConfig.ethservice.configuration.data.as.strain.canloc, loc, sizeof(loc));
    parser->convert(serviceConfig.ethservice.configuration.data.as.strain.boardtype.firmware, fir, sizeof(fir));
    parser->convert(serviceConfig.ethservice.configuration.data.as.strain.boardtype.protocol, pro, sizeof(pro));

    yInfo() << "The embObjFTsensor device using BOARD" << boardname << " w/ IP" << ipv4 << "has the following service config:";
    yInfo() << "- acquisitionrate =" << serviceConfig.acquisitionrate;
    yInfo() << "- useCalibration =" << serviceConfig.useCalibration;
    yInfo() << "- STRAIN of type" << boardtype << "named" << serviceConfig.nameOfStrain << "@" << loc << "with required protocol version =" << pro << "and required firmware version =" << fir;
    delete parser;
}


void embObjFTsensor::cleanup(void)
{
    if(GET_privData(mPriv).ethManager == NULL) return;

    int ret = GET_privData(mPriv).ethManager->releaseResource2(GET_privData(mPriv).res, this);
    GET_privData(mPriv).res = NULL;
    if(ret == -1)
        GET_privData(mPriv).ethManager->killYourself();
}

// eof

