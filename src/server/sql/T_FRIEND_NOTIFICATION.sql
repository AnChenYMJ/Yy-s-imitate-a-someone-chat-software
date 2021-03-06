/*
Date: 2021-03-25 19:19:22
*/

SET FOREIGN_KEY_CHECKS=0;

-- ----------------------------
-- Table structure for T_FRIEND_NOTIFICATION
-- 添加好友请求的记录表，一行中存入俩ID，这俩ID正在发起好友请求
-- ----------------------------
DROP TABLE IF EXISTS `T_FRIEND_NOTIFICATION`;
CREATE TABLE `T_FRIEND_NOTIFICATION` (
  `f_user_id1` bigint(20) NOT NULL,
  `f_user_id2` bigint(20) NOT NULL,
  KEY `f_u_i_fkey` (`f_user_id1`),			--申请者
  KEY `f_u_i_fkey2` (`f_user_id2`),			--接受者
  CONSTRAINT `f_u_i_fkey2` FOREIGN KEY (`f_user_id2`) REFERENCES `T_USER` (`f_user_id`),
  CONSTRAINT `f_u_i_fkey` FOREIGN KEY (`f_user_id1`) REFERENCES `T_USER` (`f_user_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
