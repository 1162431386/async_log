#如果没有bash，可以换成#!/bin/sh  
#!/bin/bash    



#编译服务器
make cmd_svr
#编译客户端
make cmd_cli

#执行py脚本，将自定义的命令读出来转化成软连接的shell脚本，没有python3的可以改成python2
python3 ./build_list.py  ./ && \
#拷贝生成的sh脚本到/bin目录下面
mv -vf cmd_list  /bin/cmd_list.sh && \

mv -vf cmd_cli   /var/

#执行shell脚本
chmod 777 /bin/cmd_list.sh
/bin/cmd_list.sh

#启动服务器
sudo ./cmd_svr 