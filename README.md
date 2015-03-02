# hpinger
equipment pinger for Hydra billing

Тулза для быстрого многопоточного пинга коммутаторов, хранящихся в АСР Гидра.

Требования:
- Oracle instant client

Библиотеки:
- boost-thread
- boost-program-options
- config
- oping

Установка на примере debian:

- aptitude install libboost-dev
- aptitude install libboost-program-options-dev
- aptitude install libboost-thread-dev
- aptitude install libconfig++-dev
- aptitude install liboping-dev

~~~
git clone https://github.com/ircop/hpinger.git
cd hpinger
mkdir build && cd build
cmake ..
make
cp hpinger /usr/bin/hpinger
cp ../contrib/hpinger.conf /etc/
~~~
../contrib/hpinger.init - init-скрипт для gentoo. Для debian'а сами готовьте. Инстантклиент тоже сами ставьте :)


У нас ~3.5к свитчей обходит за ~ 2 минуты.

--

Параметры конфига:

- switch_root_id - ID позиции "Коммутатор" в номенклатуре, которая является родительской для пингуемых свитчей.
- alive_param_id - ID доп.параметра для коммутаторов, определяющего, "живой" он, или нет. Тип - флаг. Как-то так: <img src="http://i.imgur.com/OZR5r2Z.png">
