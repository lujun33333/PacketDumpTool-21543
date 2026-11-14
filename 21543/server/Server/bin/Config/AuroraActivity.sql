-- Aurora upload endpoint settings are stored in mysql.ini under
-- [aurora_storage]. The update-server setup writes that section and renders
-- the matching Nginx authorization token.
-- Run this migration explicitly with an administrative account. SpeedServer
-- only connects to the existing activity schema and does not create it.

CREATE DATABASE IF NOT EXISTS activity
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_general_ci;

USE activity;

CREATE TABLE IF NOT EXISTS AuroraSequence (
  SequenceName VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  NextValue BIGINT UNSIGNED NOT NULL,
  PRIMARY KEY (SequenceName)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT IGNORE INTO AuroraSequence (SequenceName, NextValue)
VALUES ('work_id', 1100000000);

INSERT IGNORE INTO AuroraSequence (SequenceName, NextValue)
VALUES ('object_id', UNIX_TIMESTAMP());

CREATE TABLE IF NOT EXISTS AuroraWork (
  WorkID BIGINT UNSIGNED NOT NULL,
  OwnerUin INT UNSIGNED NOT NULL,
  NameRaw VARBINARY(64) NOT NULL,
  DescriptionRaw VARBINARY(255) NOT NULL,
  Difficulty TINYINT UNSIGNED NOT NULL DEFAULT 0,
  MaxPlayers TINYINT UNSIGNED NOT NULL DEFAULT 0,
  LifecycleState TINYINT UNSIGNED NOT NULL DEFAULT 0,
  QualificationStatus TINYINT UNSIGNED NOT NULL DEFAULT 0,
  CurrentVersion INT UNSIGNED NOT NULL DEFAULT 1,
  ComponentCount INT NOT NULL DEFAULT 0,
  RenderMd5 CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  RenderObjectKey VARCHAR(255) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  RenderUrl VARCHAR(768) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  SettingsMd5 CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  SettingsObjectKey VARCHAR(255) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  SettingsUrl VARCHAR(768) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  RowVersion INT UNSIGNED NOT NULL DEFAULT 0,
  CreatedAtMs BIGINT UNSIGNED NOT NULL,
  UpdatedAtMs BIGINT UNSIGNED NOT NULL,
  PRIMARY KEY (WorkID),
  KEY idx_AuroraWork_OwnerUpdated (OwnerUin, UpdatedAtMs),
  KEY idx_AuroraWork_StateUpdated (LifecycleState, UpdatedAtMs)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS AuroraWorkTag (
  WorkID BIGINT UNSIGNED NOT NULL,
  TagType TINYINT UNSIGNED NOT NULL,
  TagID INT UNSIGNED NOT NULL,
  PRIMARY KEY (WorkID, TagType, TagID),
  CONSTRAINT fk_AuroraWorkTag_Work
    FOREIGN KEY (WorkID) REFERENCES AuroraWork (WorkID)
    ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS AuroraUploadTicket (
  UploadID BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  WorkID BIGINT UNSIGNED NOT NULL,
  OwnerUin INT UNSIGNED NOT NULL,
  Operation TINYINT UNSIGNED NOT NULL,
  CosCommand TINYINT UNSIGNED NOT NULL,
  RequestTimestamp INT UNSIGNED NOT NULL,
  WorkVersion INT UNSIGNED NOT NULL,
  ClientRequestID VARBINARY(40) NOT NULL,
  ExpectedMd5 CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  ObjectID BIGINT UNSIGNED NOT NULL,
  ObjectKey VARCHAR(255) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  PublicUrl VARCHAR(768) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  AuthHash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  Status TINYINT UNSIGNED NOT NULL DEFAULT 0,
  ReportResultID SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  CreatedAtMs BIGINT UNSIGNED NOT NULL,
  ReportedAtMs BIGINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (UploadID),
  UNIQUE KEY uq_AuroraUpload_ObjectKey (ObjectKey),
  KEY idx_AuroraUpload_Work (WorkID, WorkVersion, CosCommand),
  KEY idx_AuroraUpload_OwnerStatus (OwnerUin, Status),
  CONSTRAINT fk_AuroraUpload_Work
    FOREIGN KEY (WorkID) REFERENCES AuroraWork (WorkID)
    ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
