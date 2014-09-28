一 简介:
	此工具是在 http://pyropus.ca/software/memtester/ 基础上进行修改而成的.主要应用于android老化内存测试.
	
二 使用说明:
	官方版本可以通过执行命令: memtester -p 10M(测试内存大小) 10 (测试循环次数, 无则表示无限循环), 修改过后的版本，server端创建了一个名为memorytester的socket, 在client apk中向该socket发送指令传递测试参数, 让server端去执行内存测试.并将打印的log通过socket回传给apk去显示.

三 配置说明:
	1. 在init.rc中添加服务:
		service memorytester /system/bin/memtester
			class main
			socket memorytester stream 0666 root system
			disabled
	2.将server的Android.mk中的LOCAL_MODULE 加入到common.mk以编译进文件系统.
		PRODUCT_PACKAGES += \
			memtester
	3.在client中通过
		SystemProperties.set("ctl.start", "memorytester");
		SystemProperties.set("ctl.stop", "memorytester");
	来开启测试和关闭测试.

四 其他:
	更好的处理方式应该通过jni去调用接口,将memtester的log打印通过回调callback方法的方式去显示. 最初是按照这思路去处理,但之前是执行memtester -p 0x0a0000 4k 1, 一直提示无权限进行操作. 所以后面就改用socket方式,通过init.rc去提升权限.后面有时间再研究通过jni方法去调用提示权限不足的原因.
