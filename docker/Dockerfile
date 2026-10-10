# VitaSDK plus a libcurl built against mbedTLS (the SDK's own libcurl was
# compiled against OpenSSL 1.0 headers but ships with OpenSSL 1.1.1, so it
# does not link). Same flags as vitasdk/packages/curl, TLS swapped.
FROM vitasdk/vitasdk:latest

ARG CURL_VER=8.17.0
ARG CURL_SHA=e8e74cdeefe5fb78b3ae6e90cd542babf788fa9480029cfcee6fd9ced42b7910

RUN cd /tmp \
 && wget -q https://github.com/curl/curl/releases/download/curl-$(echo $CURL_VER | tr . _)/curl-$CURL_VER.tar.gz \
 && echo "$CURL_SHA  curl-$CURL_VER.tar.gz" | sha256sum -c - \
 && tar xzf curl-$CURL_VER.tar.gz && cd curl-$CURL_VER && mkdir build && cd build \
 && cmake .. -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake -DCMAKE_INSTALL_PREFIX=/opt/curl-mbed \
      -DCMAKE_BUILD_TYPE=Release -DBUILD_CURL_EXE=OFF -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF \
      -DENABLE_IPV6=OFF -DCURL_DISABLE_SOCKETPAIR=ON -DHAVE_FCNTL_O_NONBLOCK=OFF -DENABLE_THREADED_RESOLVER=OFF \
      -DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF -DENABLE_CURL_MANUAL=OFF -DCURL_USE_LIBPSL=OFF \
      -DCURL_USE_OPENSSL=OFF -DCURL_USE_MBEDTLS=ON -DCURL_ZSTD=OFF -DCURL_BROTLI=OFF -DUSE_NGHTTP2=OFF \
      -DUSE_LIBIDN2=OFF -DCURL_USE_LIBSSH2=OFF -DCURL_DISABLE_LDAP=ON \
      -DCURL_CA_BUNDLE="vs0:data/external/cert/CA_LIST.cer" >/dev/null \
 && sed -i '/HAVE_PIPE2/d' lib/curl_config.h \
 && make -j$(nproc) >/dev/null && make install >/dev/null \
 && rm -rf /tmp/curl-*
