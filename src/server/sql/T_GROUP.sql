/*
Date: 2021-03-25 19:19:30
*/

SET FOREIGN_KEY_CHECKS=0;

-- ----------------------------
-- Table structure for T_GROUP
-- 此表 记录着 群聊的相关信息
-- ----------------------------
DROP TABLE IF EXISTS `T_GROUP`;
CREATE TABLE `T_GROUP` (
  `f_group_id` bigint(20) NOT NULL AUTO_INCREMENT,		--群聊id，用的主键自增长
  `f_group_name` varchar(64) NOT NULL,					--群聊名称
  `f_group_owner_id` bigint(20) DEFAULT NULL,			--群聊拥有者名称
  PRIMARY KEY (`f_group_id`),
  KEY `f_g_f_key` (`f_group_owner_id`),
  CONSTRAINT `f_g_f_key` FOREIGN KEY (`f_group_owner_id`) REFERENCES `T_USER` (`f_user_id`)
) ENGINE=InnoDB AUTO_INCREMENT=10122 DEFAULT CHARSET=utf8;
