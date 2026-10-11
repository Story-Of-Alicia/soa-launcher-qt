#!/usr/bin/env bash

soa_wine_debug() {
    case "${1:-normal}" in
    off)

        echo "-all"
        ;;
    normal)

        echo "+timestamp,+pid,+tid,err+all,fixme+all,+winediag,+loaddll"
        ;;
    verbose)

        echo "+timestamp,+pid,+tid,err+all,fixme+all,+winediag,+loaddll,+module,+process,+thread,+seh,+d3d,+d3d9,+dsound,+mmdevapi,+winmm"
        ;;
    audio)

        echo "+timestamp,+pid,+tid,err+all,fixme+all,+winediag,+loaddll,+virtual,+dsound,+mmdevapi,+winmm,+coreaudio"
        ;;
    forensic)

        echo "+timestamp,+pid,+tid,err+all,fixme+all,+winediag,+loaddll,+module,+process,+thread,+seh,+d3d,+d3d9,+dsound,+mmdevapi,+winmm,+reg,+file,+win,+msg,+sync,+heap,+virtual,+imm,+ntdll"
        ;;
    relay)

        echo "+timestamp,+pid,+tid,err+all,fixme+all,+relay,+snoop,+seh,+loaddll,+module"
        ;;
    *)
        echo "soa_wine_debug: unknown mode '$1'" >&2
        echo "modes: off normal verbose audio forensic relay" >&2
        return 2
        ;;
    esac
}

soa_wine_debug_modes() { echo "off normal verbose audio forensic relay"; }

if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
    soa_wine_debug "$@"
fi
