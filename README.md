<h1>IMPORTANT NOTICE</h1>

Due to personal reasons, most source codes **have been taken down** from this public repository. Only APIs and some examples of using the library are here.

If you are interesed in using the streamc library, please send request to <ins>yeli[AT]ntu.edu.cn</ins> from an **institutional email** with your name. 

Please note that public domain emails (such as qq.com, gmail.com, hotmail.com etc.) will **NOT** get a reply.

<h1>Introduction</h1>
This project implements packet-level forward-erasure correction (FEC) codes called _streaming codes_, which encode packets in a convolutional manner to address packet erasures. The codes are also referred to as _sliding-window linear codes_ in the literature. The key characteristic of the codes is that the packets at the destination would be recovered and delivered to upper layers in a _smoothly_ in-order manner, instead of all-together as in conventional block codes which incurs block coding delay.

The FEC code can be compiled as a shared library _libstreamc.so_, and accessed via APIs defined in _streamcodec.h_. A python wrapper is also provided as _pystreamc.py_. The following APIs are typically called to use the code in an application. 

On the encoder side:
1. Create an encoder using `initialize_encoder()`
2. Enqueue data packets into the encoder by calling `enqueue_packet()`
3. Send a source packet from the encoder by calling `output_source_packet()`. Note that the encoder will bookkeep internally and the source packets are output in a first-in-first-out (FIFO) manner, i.e., in order as they were enqueued into the encoder.
4. Send a repair packet from the encoder by calling `output_repair_packet()` or `output_repair_packet_short()`. The former encodes a repair packet across a _full_ encoding window (EW) that includes all the source packets that have been sent from the queue, whereas the latter encodes across a _short_ EW with a given width.


On the decoder side:
1. Create a decoder using `initialize_decoder()`
2. When a packet is received, input it to the decoder by calling `receive_packet()`. The received packet will be processed internally and the source packets will be made available to be accessed in the same FIFO manner as on the encoder side. This most recent in-order source packet's ID is indicated by the decoder's `inorder` variable. The corresponding packet is accessed via the `recovered` array, indexed at `inorder % DEC_ALLOC`, where `DEC_ALLOC` is a macro in the API header for specifying the decoder buffer size.

Note that it is the application's responsibility to determine when to send a source or repair packet, and whether send a full or short repair packet. This affects the end-to-end delay and computational cost. A proper choice would consitute a well-designed coded transmission scheme, which is an attractive research topic.

The library also provides several other auxiliary functions, such as for searializing data packets to byte string and vice versa, flushing certain packets earlier than an ID from the encoder (e.g. based on ACK), and free the encoder/decoder context, etc. To adopt the FEC code, it is highly recommended to read through _streamcodec.h_, and the examples provided in _examples_ folder. A full application that uses this library via the python wrapper, which is a TCP performance enhancing proxy enhanced by the streaming FEC, can also be found at https://github.com/yeliqseu/pepesc.

**_Note_: if you find the codes useful, please cite the following paper whenever appropriate.**
> Y. Li, X. Chen, Y. Hu, R. Gao, J. Wang and S. Wu, "Low-Complexity Streaming Forward Erasure Correction for Non-Terrestrial Networks," in IEEE Transactions on Communications, 2023. (Early Access: https://ieeexplore.ieee.org/document/10246292)

> Y. Li, F. Zhang, J. Wang, T. Q. S. Quek and J. Wang, "On Streaming Coding for Low-Latency Packet Transmissions Over Highly Lossy Links," in IEEE Communications Letters, vol. 24, no. 9, pp. 1885-1889, Sept. 2020 (https://ieeexplore.ieee.org/document/9075270).

