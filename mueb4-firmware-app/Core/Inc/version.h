/**
 * Contains current firmware version.
 * @file
 * @author Ádám Kiss
 * @author Zsombor Bodnár
 */

#ifndef MUEB4_FIRMWARE_CORE_INC_VERSION_H_
#define MUEB4_FIRMWARE_CORE_INC_VERSION_H_

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

const char* const mueb_version =

    "v4.0.2"

#ifdef _COMMIT
    "-" STR(_COMMIT)
#endif

#ifdef _DIRTYTREE
        "-dirty"
#endif

    ;

#endif  // MUEB4_FIRMWARE_CORE_INC_VERSION_H_
