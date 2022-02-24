**_Note_: if you find the codes useful, please cite the following paper when appropriate.**
> Y. Li, F. Zhang, J. Wang, T. Q. S. Quek and J. Wang, "On Streaming Coding for Low-Latency Packet Transmissions Over Highly Lossy Links," in IEEE Communications Letters, vol. 24, no. 9, pp. 1885-1889, Sept. 2020 (https://ieeexplore.ieee.org/document/9075270).

This project implements packet-level forward-erasure correction (FEC) codes called _streaming codes_, which encode packets in a convolutional manner to address packet erasures. The codes are also referred to as _sliding-window linear codes_ in the literature. The key characteristic of the codes is that the packets at the destination would be recovered and delivered to upper layers in a _smoothly_ in-order manner, instead of all-together as in conventional block codes which incurs block coding delay. The library can be tested by

```shell
$ make streamcTest
$ ./streamcTest
Usage: ./streamcTest snum arrival repfreq epsilon Tp irange pos1 pos2 ... 
                       snum     - maximum number of source packets to transmit
                       arrival  - Bernoulli arrival rate at the sending queue, value: [0, 1]
                                  0 - all source packets available before time 0
                       repfreq  - frequency of inserting repair packets
                                  random insertion of repair packets if repfreq < 1
                                  fixed-interval insertion if repfreq >= 1 (must be integer)
                       epsilon  - erasure probability of the end-to-end link
                       Tp       - propagation delay of channel
                       irange   - period of the irregular pattern (0: regular or random depending on repfreq)
                       posX     - positions sending source packets in the irregular range
```
Note that _pos1_, _pos2_, ... arguments can be omitted if _irange_ is set to 0.

The simulation script would stream up to _snum_ source packets over a lossy link of erasure probability specified by _epsilon_ and propagation delay _Tp_. Given a transmission opportunity, this proto-type implementation determines to send either a new source packet in its original form or a repair packets which are random linear combinations of the previously sent source packets, depending on the configuration as detailed in the following.

**Configuration 1**: Send a repair packet after sending every _l_ source packets. For example,
```shell
$ ./streamcTest 1000 0 2 0.1 10 0
```
which streams 1000 source packets where the repair packets are inserted every _2_ source packets. The most frequent insertion of repair packets in this way is _1_. 

**Configuration 2**: Send a repair packet with probability _f_, or otherwise a source packet. For example,
```shell
$ ./streamcTest 1000 0 0.6 0.5 10 0
```
would send repair packet with probability _f=0.6_, which can achieve finite in-order delay over a link of erasure probability _0.5_, for which even sending repair packets every 1 source packet is not enough.

**Configuration 3**: Send source/repair packets at specified time by specifying positions in a time window at which source packets are sent. For example,
```shell
$ ./streamcTest 1000 0 0.6 0.5 10 10 1 4 7
```
would send repair packets at positions other than 1, 4, 7 in every 10-slot time window, i.e., source packets are sent at time 1, 4, 7, 11, 14, 17, 21, 24, 27, ....

