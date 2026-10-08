# The binary is built in EF800_Devcontainer. This image is deliberately only
# a minimal glibc runtime: native_sim is a 32-bit dynamically linked Linux
# executable and requires /lib/ld-linux.so.2, libc, and libm.
FROM i386/debian:bookworm-slim

RUN groupadd --system virtualef800 \
    && useradd --system --gid virtualef800 --home-dir /opt/virtual-ef800 \
       --create-home virtualef800
WORKDIR /opt/virtual-ef800
COPY build/ef800-main-native-sim/zephyr/zephyr.exe ./zephyr.exe
COPY docker/virtual-ef800/entrypoint.sh /usr/local/bin/virtual-ef800-entrypoint
RUN chown virtualef800:virtualef800 ./zephyr.exe \
    && chmod 0555 ./zephyr.exe \
    && chmod 0555 /usr/local/bin/virtual-ef800-entrypoint \
    && mkdir -p /var/lib/virtual-ef800 \
    && chown virtualef800:virtualef800 /var/lib/virtual-ef800
EXPOSE 8800
ENTRYPOINT ["/usr/local/bin/virtual-ef800-entrypoint"]
