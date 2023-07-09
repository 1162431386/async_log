#!/usr/bin/python
# -*- coding:UTF-8 -*-

import os
import os.path
import time
import sys

cmd_list = 'app_cmd_list.h'
ln_file = 'cmd_list'
#ln_cmd = "ln -s /bin/datura "
ln_cmd = "ln -s /var/cmd_cli"
g_filePath = None

list_path = {}
def cmd_strings_get(file):
    fp = open(file)
    lines = fp.readlines()
    ln_cmd = {}
    b = 0
    for i in range(len(lines)):
        lines[i] = lines[i].replace('\r', '')
        lines[i] = lines[i].replace('\n', '')
        lines[i] = lines[i].replace('\\', '')
        strings = lines[i].find('CMD_ENTRY')
        if strings != -1:
            str_cmd = lines[i].split('(')
            ln_cmd[b] = str_cmd[1].split(',')[0]
            b += 1
    fp.close()
    return ln_cmd

def cmd_list_create(file):
    list = cmd_strings_get(file)
    fp = open(ln_file, 'w+')
    print("Total %d comands need to be ln -s" %(len(list)))
    for i in range(len(list)):
        print(list[i])
        fp.writelines(ln_cmd + ' /bin/' + list[i] + '\n')

def cmd_search_file(path, file):
    for item in os.listdir(path):
        item_path = os.path.join(path, item)
        #print(item_path)
        if os.path.isdir(item_path):
            cmd_search_file(item_path, file)
        elif os.path.isfile(item_path) and file == os.path.split(item_path)[1]:
            global g_filePath
            g_filePath = item_path

def main(path): 
    cmd_search_file(path, cmd_list)
    print(g_filePath)
    if g_filePath:
        cmd_list_create(g_filePath)
    else:
        print("Can't search the %s file."%(cmd_list))

if __name__ == "__main__" :
    main(sys.argv[1])
