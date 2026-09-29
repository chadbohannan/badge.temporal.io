#pragma once

// Hardware target selector.
// Set via build_flags in platformio.ini: -DHARDWARE_DELTA or -DHARDWARE_CHARLIE

#if defined(HARDWARE_DELTA)
  #include "DeltaDefines.h"
#elif defined(HARDWARE_CHARLIE)
  #include "CharlieDefines.h"
#elif defined(HARDWARE_ECHO)
  #include "EchoDefines.h"
#elif defined(HARDWARE_HOST)
  // Host test harness: firmware/host/HostDefines.h, found through the
  // [env:host] include path.
  #include "HostDefines.h"
#else
  #error "No hardware target defined. Add -DHARDWARE_DELTA, -DHARDWARE_CHARLIE, -DHARDWARE_ECHO, or -DHARDWARE_HOST to platformio.ini build_flags."
#endif
