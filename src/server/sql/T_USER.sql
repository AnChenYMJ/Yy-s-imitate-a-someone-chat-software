/*
Navicat MySQL Data Transfer

Source Server         : 111
Source Server Version : 50568
Source Host           : 121.36.69.144:3306
Source Database       : tengxun

Target Server Type    : MYSQL
Target Server Version : 50568
File Encoding         : 65001

Date: 2021-03-25 19:20:05
*/

SET FOREIGN_KEY_CHECKS=0;

-- ----------------------------
-- Table structure for T_USER
-- ----------------------------
DROP TABLE IF EXISTS `T_USER`;
CREATE TABLE `T_USER` (
  `f_user_id` bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID\n',	--AUTO_INCREMENT 设置为主键自增长 就不需要用户再输入数据了
  `f_password` varchar(64) NOT NULL COMMENT '用户密码',				--创建新表的脚本中， 可在字段定义脚本中添加comment属性来添加注释。 注释就是COMMENT 后的字符串内容
  `f_user_name` varchar(64) NOT NULL COMMENT '用户名',
  `f_online` bigint(20) NOT NULL COMMENT '用户ID',					--是否在线的标识
  `f_photo` varchar(64) DEFAULT NULL,								--默认值约束（Default Constraint） ”，用来指定某列的默认值。如果没有为某个字段赋值，系统就会自动为这个字段插入默认值。
  `f_signature` varchar(64) DEFAULT NULL,
  PRIMARY KEY (`f_user_id`),										--主键约束 主键值必须唯一标识表中的每一行，且不能为 NULL，不可重复，每个表只能定义一个主键。
  UNIQUE KEY `T_USER_pk_2` (`f_user_id`),							--唯一约束（Unique Key）是指所有记录中字段的值不能重复出现。
  KEY `T_USER_f_user_id_index` (`f_online`)
) ENGINE=InnoDB AUTO_INCREMENT=10122 DEFAULT CHARSET=utf8 COMMENT='用户信息表';
