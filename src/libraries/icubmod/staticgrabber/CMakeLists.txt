# Copyright: (C) 2010 RobotCub Consortium
# Authors: Lorenzo Natale
# CopyPolicy: Released under the terms of the GNU GPL v2.0.

yarp_prepare_device(static_grabber TYPE yarp::dev::StaticGrabber INCLUDE StaticGrabber.h
				WRAPPER grabber)

if (NOT SKIP_static_grabber)
  include_directories(${YARP_INCLUDE_DIRS})

  yarp_add_plugin(static_grabber StaticGrabber.h StaticGrabber.cpp)

  target_link_libraries(static_grabber ${YARP_LIBRARIES})
  
  icub_export_plugin(static_grabber)
  yarp_install(FILES static_grabber.ini  DESTINATION ${ICUB_PLUGIN_MANIFESTS_INSTALL_DIR})
endif (NOT SKIP_static_grabber)


