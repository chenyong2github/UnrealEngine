FROM alpine:3.6
WORKDIR /home
RUN apk --no-cache add  automake autoconf build-base libtool libusb-dev openssl-dev git
RUN git clone https://github.com/libimobiledevice-win32/libplist \
&& git clone https://github.com/libimobiledevice-win32/libusbmuxd \
&& git clone https://github.com/libimobiledevice-win32/libimobiledevice \
&& git clone https://github.com/libimobiledevice-win32/usbmuxd
RUN cd /home/libplist \
&& ./autogen.sh --without-cython \
&& make \
&& make install \
&& make install DESTDIR=/home/out \
&& cd /home/libusbmuxd \
&& ./autogen.sh --without-cython \
&& make \
&& make install \
&& make install DESTDIR=/home/out \
&& cd /home/libimobiledevice \
&& ./autogen.sh --without-cython \
&& make \
&& make install \
&& make install DESTDIR=/home/out \
&& cd /home/usbmuxd \
&& ./autogen.sh \
&& make \
&& make install \
&& make install DESTDIR=/home/out
RUN cd /home/out \
&& tar -zcvf /home/usbmuxd.alpine.tar.gz -C /home/out/ .

CMD [ "cp", "/home/usbmuxd.alpine.tar.gz", "/build/usbmuxd.alpine.tar.gz" ]
