#ifndef __SHARED_CAN_BUS_H__
#define __SHARED_CAN_BUS_H__

#include <yarp/os/Semaphore.h>
#include <yarp/dev/DeviceDriver.h>
#include <yarp/dev/CanBusInterface.h>

#include "canControlConstants.h"

#define UNREQ 0
#define REQST 1

namespace yarp{
    namespace dev{
        class CanBusAccessPoint;
    }
}

class yarp::dev::CanBusAccessPoint : 
    public ICanBus, 
    public ICanBufferFactory,
    public DeviceDriver
{
public:
    CanBusAccessPoint() : waitReadMutex(0),synchroMutex(1)
    {
        for (int i=0; i<0x800; ++i) reqIds[i]=UNREQ;

        readBuffer=createBuffer(BUF_SIZE);

        waitingOnRead=false;

        nRecv=0;
    }

    ~CanBusAccessPoint()
    {
        destroyBuffer(readBuffer);
    }

    bool hasId(unsigned int id)
    {
        return reqIds[id]==REQST;
    }

    bool pushReadMsg(CanMessage& msg)
    {
        synchroMutex.wait();

        if (nRecv>=BUF_SIZE)
        {
            synchroMutex.post();
            fprintf(stderr, "Warning: recv buffer overrun\n");
            return false;
        }

        readBuffer[nRecv++]=msg;
        
        if (waitingOnRead)
        {
            waitingOnRead=false;
            waitReadMutex.post();
        }
        
        synchroMutex.post();
        return true;
    }

    ////////////
    // ICanBus
    virtual bool canGetBaudRate(unsigned int *rate);
    virtual bool canSetBaudRate(unsigned int rate)
    {
        fprintf(stderr, "Error: set baud rate not allowed from CanBusAccessPoint implementation\n");
        return false;
    }

    virtual bool canIdAdd(unsigned int id);
    virtual bool canIdDelete(unsigned int id);

    virtual bool canRead(CanBuffer &msgs, unsigned int size, unsigned int *nmsg, bool wait=false)
    {
        synchroMutex.wait();

        if (wait && !nRecv)
        {
            waitingOnRead=true;
            synchroMutex.post();
            waitReadMutex.wait();
            synchroMutex.wait();
        }

        if (nRecv)
        {
            for (unsigned int i=0; i<nRecv && i<size; ++i)
            {
                msgs[i]=readBuffer[i];
            }

            *nmsg=nRecv;
            nRecv=0;
            synchroMutex.post();
            return true;
        }
        else
        {
            *nmsg=0;
            synchroMutex.post();
            return true;
        }
    }

    virtual bool canWrite(const CanBuffer &msgs, unsigned int size, unsigned int *sent, bool wait=false);
    // ICanBus
    ////////////
    
    //////////////////////
    // ICanBufferFactory
    virtual CanBuffer createBuffer(int nmessage);
    virtual void destroyBuffer(CanBuffer &msgs);
    // ICanBufferFactory
    //////////////////////

    /////////////////
    // DeviceDriver
    virtual bool open(yarp::os::Searchable& config);
    virtual bool close();
    // DeviceDriver
    /////////////////

protected:
    yarp::os::Semaphore waitReadMutex;
    yarp::os::Semaphore synchroMutex;
    
    bool waitingOnRead;

    unsigned int nRecv;
    CanBuffer readBuffer;
    
    char reqIds[0x800];
};

#endif

