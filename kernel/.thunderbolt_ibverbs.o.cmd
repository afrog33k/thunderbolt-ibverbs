savedcmd_thunderbolt_ibverbs.o := ld -EL  -maarch64linux -z norelro -z noexecstack --no-warn-rwx-segments   -r -o thunderbolt_ibverbs.o @thunderbolt_ibverbs.mod 
