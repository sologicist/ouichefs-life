path="/users/Etu2/28705252/Documents/M1/PNL/projet/kernel"
HDA="-drive file=/tmp/pnl_2023-2024/lkp-arch.img,format=raw"
HDB="-drive file=$path/myHome.img,format=raw"
SHARED="$path/share"
KERNEL=/tmp/pnl_2023-2024/linux-6.5.7/arch/x86/boot/bzImage
KDB="yes"

if [ -z ${KDB} ]; then
    CMDLINE='root=/dev/sda1 rw console=ttyS0 kgdboc=ttyS1'
else
    CMDLINE='root=/dev/sda1 rw console=ttyS0 kgdboc=ttyS1 kgdbwait'
fi

FLAGS="--enable-kvm "
VIRTFS+=" --virtfs local,path=${SHARED},mount_tag=share,security_model=passthrough,id=share "

exec qemu-system-x86_64 ${FLAGS} \
     ${HDA} ${HDB} \
     ${VIRTFS} \
     -net user -net nic \
     -serial mon:stdio -serial tcp::1234,server,nowait \
     -boot c -m 1G \
     -kernel "${KERNEL}" \
     -initrd $path/my_initramfs.cpio.gz \
     -append "${CMDLINE}" \
     -nographic
