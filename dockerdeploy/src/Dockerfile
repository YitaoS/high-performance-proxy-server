From ubuntu:20.04
RUN apt-get update && \
    apt-get install -yq tzdata && \
    ln -fs /usr/share/zoneinfo/America/New_York /etc/localtime && \
    dpkg-reconfigure -f noninteractive tzdata
ENV TZ="America/New_York"

RUN apt-get update
RUN apt-get install -y g++ make libboost-all-dev
RUN mkdir /var/log/erss
RUN touch /var/log/erss/proxy.log
RUN mkdir /code
WORKDIR /code
ADD my_proxy/. /code/
RUN chmod o+x launch.sh
