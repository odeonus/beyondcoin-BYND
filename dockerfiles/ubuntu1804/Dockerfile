FROM ubuntu:18.04
COPY ./beyondcoin.conf /root/.beyondcoin/beyondcoin.conf
COPY . /beyondcoin
WORKDIR /beyondcoin
#install shared libraries and dependencies
RUN sudo apt update
RUN sudo apt-get install -y build-essential libtool autotools-dev automake pkg-config libssl-dev libevent-dev bsdmainutils
RUN sudo apt-get install -y libboost-system-dev libboost-filesystem-dev libboost-chrono-dev libboost-program-options-dev libboost-test-dev libboost-thread-dev
#install BerkleyDB for wallet support
RUN sudo apt-get install -y software-properties-common
RUN sudo add-apt-repository ppa:bitcoin/bitcoin
RUN sudo apt-get update
RUN sudo apt-get install -y libdb4.8-dev libdb4.8++-dev
#install upnp
RUN sudo apt-get install -y libminiupnpc-dev
#install ZMQ
RUN sudo apt-get install -y libzmq3-dev
#build Beyondcoin source
RUN ./autogen.sh
RUN ./configure
RUN sudo make
RUN sudo make install
#open service port
EXPOSE 10333 14333
CMD ["beyondcoind", "--printtoconsole"]
