#!/bin/sh
# Installed as bin/linux-x64/llm-video-engine in the sandbox bundle (linux-bundle.yml). Runs the
# x86-64-v3 build (AVX2, about 2x faster drawing) when the CPU has every v3 feature, the baseline
# build otherwise; a v3 binary on an older CPU would die on its first AVX2 instruction.
# LVE_BUILD=x86-64 or LVE_BUILD=x86-64-v3 forces one.
d=$(dirname "$(readlink -f "$0")")
b=${LVE_BUILD:-}
if [ -z "$b" ]; then
    b=x86-64-v3
    flags=" $(grep -m1 '^flags' /proc/cpuinfo 2>/dev/null) "
    for f in avx avx2 bmi1 bmi2 f16c fma abm movbe xsave; do
        case "$flags" in *" $f "*) ;; *) b=x86-64; break ;; esac
    done
fi
exec "$d/llm-video-engine-$b" "$@"
