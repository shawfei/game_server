/*
 * HotUpdate.cpp
 *
 *  Created on: Dec 24, 2015
 *      Author: zhangyalei
 */

#include "HotUpdate.h"
#include <dirent.h>
//#include <openssl/md5.h>
#include <fstream>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include "Game_Manager.h"

Hot_Update::Hot_Update() {

}

Hot_Update::~Hot_Update() {

}

Hot_Update *Hot_Update::instance_;

Hot_Update *Hot_Update::instance(void) {
	if (! instance_)
		instance_ = new Hot_Update;
	return instance_;
}

void Hot_Update::run_handler(void) {
	init_all_module();

	Time_Value interval = Time_Value(60);
	notify_interval_ = Time_Value(10);

	while (true) {
		Time_Value::sleep(interval);
		for (MD5_STR_MAP::iterator it = md5_str_map_.begin(); it != md5_str_map_.end(); ++it) {
			check_config(it->first);
		}

		if (!update_module_.empty() && update_time_ < Time_Value::gettimeofday()) {
			std::string module_name = update_module_.back();
			notice_update(module_name);
			update_module_.pop_back();
			if (!update_module_.empty()) {
				update_time_ = Time_Value::gettimeofday() + notify_interval_;
			}
		}
	}
}

int Hot_Update::notice_update(const std::string module) {
	MSG_400001 inner_msg;
	inner_msg.module = module;
	Block_Buffer inner_buf;
	inner_buf.make_inner_message(SYNC_INNER_CONFIG_HOTUPDATE);
	inner_msg.serialize(inner_buf);
	inner_buf.finish_message();
	GAME_MANAGER->push_self_loop_message(inner_buf);

	return 0;
}

std::string Hot_Update::file_md5(std::string file_name) {
	//MD5_CTX md5;
	unsigned char md[16];
	char tmp[33] = {'\0'};
	int length = 0, i = 0;
	char buffer[1024];
	std::string hash = "";
	//MD5_Init(&md5);

	int fd = 0;
	if ((fd = open(file_name.c_str(), O_RDONLY)) < 0) {
		return hash;
	}

	/*
	static struct flock lock;
	lock.l_type = F_WRLCK;
	lock.l_start = 0;
	lock.l_whence = SEEK_SET;
	lock.l_len = 0;
	lock.l_pid = getpid();
	if ( fcntl(fd, F_SETLKW, &lock) == -1 ) {
		close(fd);
		return hash;
	}
	*/

	while (true) {
		length = read(fd, buffer, 1024);
		if (length == 0 || ((length == -1) && (errno != EINTR))) {
			break;
		} else if (length > 0) {
			//MD5_Update(&md5, buffer, length);
		}
	}

	/*
	lock.l_type = F_UNLCK;
	lock.l_start = 0;
	lock.l_whence = SEEK_SET;
	lock.l_len = 0;
	lock.l_pid = getpid();
	fcntl(fd, F_SETLKW, &lock);
	*/

	//MD5_Final(md, &md5);
	for(i=0; i < 16; i++) {
		sprintf(tmp, "%02X", md[i]);
		hash += (std::string)tmp;
	}
	close(fd);

	return hash;
}

void Hot_Update::check_config(std::string module) {
	MD5_STR_MAP::iterator find_it = md5_str_map_.find(module);
	if (find_it == md5_str_map_.end()) return;

	bool need_once = false;
	MD5_STR_SET md5_str_set;
	get_md5_str(module, md5_str_set);
	for (MD5_STR_SET::iterator it = md5_str_set.begin(); it != md5_str_set.end(); ++it) {
		if (find_it->second.count(*it) == 0) {
			//MSG_DEBUG("config change:%s md5:%s", full_dir_path.c_str(), md5_str.c_str());
			need_once = true;
		}
	}

	if (need_once) {
		update_module_.push_back(module);
		update_time_ = Time_Value::gettimeofday() + notify_interval_;
		find_it->second = md5_str_set;
	}
}

void Hot_Update::init_all_module(void) {
	struct dirent *ent = NULL;
	DIR *pDir = NULL;
	std::string module_path = "config/";
	pDir = opendir(module_path.c_str());
	if (pDir == NULL) {
		MSG_USER("hot update open file error:%s", module_path.c_str());
		//被当作目录，但是执行opendir后发现又不是目录，比如软链接就会发生这样的情况。
		return;
	}
	MD5_STR_SET md5_str_set;
	while (NULL != (ent = readdir(pDir))) {
		if (ent->d_type & DT_DIR) {
			if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0 || strcmp(ent->d_name, ".svn") == 0) {
				continue;
			}
			//MSG_USER("hot update module :%s", ent->d_name);
			//init_md5_str(ent->d_name);
			get_md5_str(ent->d_name, md5_str_set);
			md5_str_map_.insert(std::make_pair(ent->d_name, md5_str_set));
		}
	}

	if (pDir) {
		closedir(pDir);
		pDir = NULL;
	}
}

void Hot_Update::get_md5_str(std::string path, MD5_STR_SET &md5_str_set) {
	struct dirent *ent = NULL;
	DIR *pDir = NULL;
	std::string module_path = "config/" + path;
	pDir = opendir(module_path.c_str());
	if (pDir == NULL) {
		//被当作目录，但是执行opendir后发现又不是目录，比如软链接就会发生这样的情况。
		return;
	}
	while (NULL != (ent = readdir(pDir))) {
		if (ent->d_type == 8) {
			if (!strstr(ent->d_name, ".json")) continue;

			if (strstr(ent->d_name, ".swp")) continue;

			//directory
			std::string config_name(ent->d_name);
			std::string full_dir_path = "config/" + path + "/" + config_name;

			std::string md5_str = file_md5(full_dir_path);
			if (md5_str != "")
				md5_str_set.insert(md5_str);
		} else if (ent->d_type & DT_DIR) {
			if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0 || strcmp(ent->d_name, ".svn") == 0) {
				continue;
			}
			get_md5_str(path + "/" +ent->d_name, md5_str_set);
		}
	}

	if (pDir) {
		closedir(pDir);
		pDir = NULL;
	}
}
