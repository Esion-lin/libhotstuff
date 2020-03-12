###issue(remained until last time)
----------------2.26--------------
1.异步提交milestone：无法解决异步提交milestone的问题，若leader发出proposal时其他的hotstuff节点还没收到Coordinator发步的milestone，会导致共识失败。
2.普通IOTA节点验签问题：门限签名结构为 set = [sig1(block),sig2(block),sig3(block)....]等多个节点对共识的区块的hash的签名集合，验证有效为size_of(valid_of(set)>Threshold,该结构包含动态数据结构，目前无法放入milestone中。
3.由于IOTA使用Trytes的结构，两层协议需要在多处添加数据装换(涉及到c++的unsigned char 和 java 的Trytes结构的String)，同时需要在IOTA普通节点添加新的验签函数，由于原函数使用c++编写，需要一段时间修改。