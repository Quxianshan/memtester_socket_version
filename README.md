һ ���:

	�˹������� http://pyropus.ca/software/memtester/ �����Ͻ����޸Ķ��ɵ�.��ҪӦ����android�ϻ��ڴ����. 

�� ʹ��˵��:
	�ٷ��汾����ͨ��ִ������: memtester -p 10M(�����ڴ��С) 10 (����ѭ������, �����ʾ����ѭ��), �޸Ĺ���İ汾��server�˴�����һ����Ϊmemorytester��socket, ��client apk�����socket����ָ��ݲ��Բ���, ��server��ȥִ���ڴ����.������ӡ��logͨ��socket�ش���apkȥ��ʾ.

�� ����˵��:
	1. ��init.rc����ӷ���:
		service memorytester /system/bin/memtester
			class main
			socket memorytester stream 0666 root system
			disabled
	2.��server��Android.mk�е�LOCAL_MODULE ���뵽common.mk�Ա�����ļ�ϵͳ.
		PRODUCT_PACKAGES += \
			memtester
	3.��client��ͨ��
		SystemProperties.set("ctl.start", "memorytester");
		SystemProperties.set("ctl.stop", "memorytester");
	���������Ժ͹رղ���.

�� ����:
	���õĴ���ʽӦ��ͨ��jniȥ���ýӿ�,��memtester��log��ӡͨ���ص�callback�����ķ�ʽȥ��ʾ. ����ǰ�����˼·ȥ����,��֮ǰ��ִ��memtester -p 0x0a0000 4k 1, һֱ��ʾ��Ȩ�޽��в���. ���Ժ���͸���socket��ʽ,ͨ��init.rcȥ����Ȩ��.������ʱ�����о�ͨ��jni����ȥ������ʾȨ�޲����ԭ��.
