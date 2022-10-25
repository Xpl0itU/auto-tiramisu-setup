# build wut
FROM wiiuenv/devkitppc:20220917 AS final

ENV PATH=$DEVKITPPC/bin:$PATH \
    WUT_ROOT=$DEVKITPRO/wut
WORKDIR /

RUN git clone --recursive https://github.com/yawut/libromfs-wiiu --single-branch && \
    cd libromfs-wiiu && \
    make -j$(nproc) && \
    make install && \
    cd .. && \
    rm -rf libromfs-wiiu

WORKDIR /project
