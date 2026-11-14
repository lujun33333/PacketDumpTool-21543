import { existsSync, mkdirSync, renameSync, rmSync } from 'node:fs';
import path from 'node:path';
import { DatabaseSync } from 'node:sqlite';

function assertCatalog(catalog) {
  if (!catalog || !Number.isSafeInteger(catalog.version) || catalog.version <= 0) {
    throw new TypeError('Catalog version must be a positive integer');
  }
  if (!Array.isArray(catalog.items)) {
    throw new TypeError('Catalog items must be an array');
  }
  if (catalog.sourceSha256 && !/^[a-f0-9]{64}$/iu.test(catalog.sourceSha256)) {
    throw new TypeError('Catalog source SHA-256 must contain 64 hexadecimal characters');
  }
}

export function writeItemCatalogDatabase(outputPath, catalog) {
  assertCatalog(catalog);
  const resolvedOutputPath = path.resolve(outputPath);
  const outputDirectory = path.dirname(resolvedOutputPath);
  mkdirSync(outputDirectory, { recursive: true });

  const temporaryPath = `${resolvedOutputPath}.tmp-${process.pid}`;
  rmSync(temporaryPath, { force: true });

  let database;
  try {
    database = new DatabaseSync(temporaryPath);
    database.exec(`
      PRAGMA journal_mode = OFF;
      PRAGMA synchronous = OFF;
      PRAGMA temp_store = MEMORY;

      CREATE TABLE items (
        id INTEGER PRIMARY KEY CHECK (id > 0),
        name TEXT NOT NULL,
        category TEXT NOT NULL,
        subcategory TEXT NOT NULL,
        gender TEXT NOT NULL,
        type_code INTEGER NOT NULL
      ) STRICT;

      CREATE TABLE catalog_meta (
        key TEXT PRIMARY KEY,
        value TEXT NOT NULL
      ) STRICT;
    `);

    const insertItem = database.prepare(`
      INSERT INTO items (id, name, category, subcategory, gender, type_code)
      VALUES (?, ?, ?, ?, ?, ?)
    `);
    const insertMeta = database.prepare(`
      INSERT INTO catalog_meta (key, value)
      VALUES (?, ?)
    `);

    database.exec('BEGIN IMMEDIATE');
    try {
      for (const item of catalog.items) {
        insertItem.run(
          item.id,
          item.name,
          item.category,
          item.subcategory,
          item.gender,
          item.typeCode
        );
      }
      insertMeta.run('source_version', String(catalog.version));
      insertMeta.run('source_sha256', catalog.sourceSha256 || '');
      insertMeta.run('row_count', String(catalog.items.length));
      database.exec('COMMIT');
    } catch (error) {
      database.exec('ROLLBACK');
      throw error;
    }

    database.exec(`
      CREATE INDEX idx_items_category_subcategory
      ON items(category, subcategory, id);

      CREATE INDEX idx_items_name
      ON items(name);

      ANALYZE;
      PRAGMA optimize;
    `);
    database.close();
    database = undefined;

    if (existsSync(resolvedOutputPath)) {
      rmSync(resolvedOutputPath, { force: true });
    }
    renameSync(temporaryPath, resolvedOutputPath);
    return resolvedOutputPath;
  } catch (error) {
    if (database) database.close();
    rmSync(temporaryPath, { force: true });
    throw error;
  }
}
