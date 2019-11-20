

#include "common.h"

#include "db.h"
#include "log.h"
#include "sysinfo.h"
#include "trxdbupgrade.h"
#include "dbupgrade.h"

typedef struct
{
	trx_dbpatch_t	*patches;
	const char	*description;
}
trx_db_version_t;

#ifdef HAVE_MYSQL
#	define TRX_DB_TABLE_OPTIONS	" engine=innodb"
#	define TRX_DROP_FK		" drop foreign key"
#else
#	define TRX_DB_TABLE_OPTIONS	""
#	define TRX_DROP_FK		" drop constraint"
#endif

#if defined(HAVE_IBM_DB2)
#	define TRX_DB_ALTER_COLUMN	" alter column"
#elif defined(HAVE_POSTGRESQL)
#	define TRX_DB_ALTER_COLUMN	" alter"
#else
#	define TRX_DB_ALTER_COLUMN	" modify"
#endif

#if defined(HAVE_IBM_DB2)
#	define TRX_DB_SET_TYPE		" set data type"
#elif defined(HAVE_POSTGRESQL)
#	define TRX_DB_SET_TYPE		" type"
#else
#	define TRX_DB_SET_TYPE		""
#endif

/* NOTE: Do not forget to sync changes in TRX_TYPE_*_STR defines for Oracle with trx_oracle_column_type()! */

#if defined(HAVE_IBM_DB2) || defined(HAVE_POSTGRESQL)
#	define TRX_TYPE_ID_STR		"bigint"
#elif defined(HAVE_MYSQL)
#	define TRX_TYPE_ID_STR		"bigint unsigned"
#elif defined(HAVE_ORACLE)
#	define TRX_TYPE_ID_STR		"number(20)"
#endif

#ifdef HAVE_ORACLE
#	define TRX_TYPE_INT_STR		"number(10)"
#	define TRX_TYPE_CHAR_STR	"nvarchar2"
#else
#	define TRX_TYPE_INT_STR		"integer"
#	define TRX_TYPE_CHAR_STR	"varchar"
#endif

#if defined(HAVE_IBM_DB2)
#	define TRX_TYPE_FLOAT_STR	"decfloat(16)"
#	define TRX_TYPE_UINT_STR	"bigint"
#elif defined(HAVE_MYSQL)
#	define TRX_TYPE_FLOAT_STR	"double(16,4)"
#	define TRX_TYPE_UINT_STR	"bigint unsigned"
#elif defined(HAVE_ORACLE)
#	define TRX_TYPE_FLOAT_STR	"number(20,4)"
#	define TRX_TYPE_UINT_STR	"number(20)"
#elif defined(HAVE_POSTGRESQL)
#	define TRX_TYPE_FLOAT_STR	"numeric(16,4)"
#	define TRX_TYPE_UINT_STR	"numeric(20)"
#endif

#if defined(HAVE_IBM_DB2)
#	define TRX_TYPE_SHORTTEXT_STR	"varchar(2048)"
#elif defined(HAVE_ORACLE)
#	define TRX_TYPE_SHORTTEXT_STR	"nvarchar2(2048)"
#else
#	define TRX_TYPE_SHORTTEXT_STR	"text"
#endif

#if defined(HAVE_IBM_DB2)
#	define TRX_TYPE_TEXT_STR	"varchar(2048)"
#elif defined(HAVE_ORACLE)
#	define TRX_TYPE_TEXT_STR	"nclob"
#else
#	define TRX_TYPE_TEXT_STR	"text"
#endif

#define TRX_FIRST_DB_VERSION		2010000

extern unsigned char	program_type;


#ifndef HAVE_SQLITE3
static void	DBfield_type_string(char **sql, size_t *sql_alloc, size_t *sql_offset, const TRX_FIELD *field)
{
	switch (field->type)
	{
		case TRX_TYPE_ID:
			trx_strcpy_alloc(sql, sql_alloc, sql_offset, TRX_TYPE_ID_STR);
			break;
		case TRX_TYPE_INT:
			trx_strcpy_alloc(sql, sql_alloc, sql_offset, TRX_TYPE_INT_STR);
			break;
		case TRX_TYPE_CHAR:
			trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s(%hu)", TRX_TYPE_CHAR_STR, field->length);
			break;
		case TRX_TYPE_FLOAT:
			trx_strcpy_alloc(sql, sql_alloc, sql_offset, TRX_TYPE_FLOAT_STR);
			break;
		case TRX_TYPE_UINT:
			trx_strcpy_alloc(sql, sql_alloc, sql_offset, TRX_TYPE_UINT_STR);
			break;
		case TRX_TYPE_SHORTTEXT:
			trx_strcpy_alloc(sql, sql_alloc, sql_offset, TRX_TYPE_SHORTTEXT_STR);
			break;
		case TRX_TYPE_TEXT:
			trx_strcpy_alloc(sql, sql_alloc, sql_offset, TRX_TYPE_TEXT_STR);
			break;
		default:
			assert(0);
	}
}

#ifdef HAVE_ORACLE
typedef enum
{
	TRX_ORACLE_COLUMN_TYPE_NUMERIC,
	TRX_ORACLE_COLUMN_TYPE_CHARACTER,
	TRX_ORACLE_COLUMN_TYPE_UNKNOWN
}
trx_oracle_column_type_t;

/******************************************************************************
 *                                                                            *
 * Function: trx_oracle_column_type                                           *
 *                                                                            *
 * Purpose: determine whether column type is character or numeric             *
 *                                                                            *
 * Parameters: field_type - [IN] column type in Treegix definitions            *
 *                                                                            *
 * Return value: column type (character/raw, numeric) in Oracle definitions   *
 *                                                                            *
 * Comments: The size of a character or raw column or the precision of a      *
 *           numeric column can be changed, whether or not all the rows       *
 *           contain nulls. Otherwise in order to change the datatype of a    *
 *           column all rows of the column must contain nulls.                *
 *                                                                            *
 ******************************************************************************/
static trx_oracle_column_type_t	trx_oracle_column_type(unsigned char field_type)
{
	switch (field_type)
	{
		case TRX_TYPE_ID:
		case TRX_TYPE_INT:
		case TRX_TYPE_FLOAT:
		case TRX_TYPE_UINT:
			return TRX_ORACLE_COLUMN_TYPE_NUMERIC;
		case TRX_TYPE_CHAR:
		case TRX_TYPE_SHORTTEXT:
		case TRX_TYPE_TEXT:
			return TRX_ORACLE_COLUMN_TYPE_CHARACTER;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			return TRX_ORACLE_COLUMN_TYPE_UNKNOWN;
	}
}
#endif

static void	DBfield_definition_string(char **sql, size_t *sql_alloc, size_t *sql_offset, const TRX_FIELD *field)
{
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, TRX_FS_SQL_NAME " ", field->name);
	DBfield_type_string(sql, sql_alloc, sql_offset, field);
	if (NULL != field->default_value)
	{
		char	*default_value_esc;

#if defined(HAVE_MYSQL)
		switch (field->type)
		{
			case TRX_TYPE_BLOB:
			case TRX_TYPE_TEXT:
			case TRX_TYPE_SHORTTEXT:
			case TRX_TYPE_LONGTEXT:
				/* MySQL: BLOB and TEXT columns cannot be assigned a default value */
				break;
			default:
#endif
				default_value_esc = DBdyn_escape_string(field->default_value);
				trx_snprintf_alloc(sql, sql_alloc, sql_offset, " default '%s'", default_value_esc);
				trx_free(default_value_esc);
#if defined(HAVE_MYSQL)
		}
#endif
	}

	if (0 != (field->flags & TRX_NOTNULL))
	{
#if defined(HAVE_ORACLE)
		switch (field->type)
		{
			case TRX_TYPE_INT:
			case TRX_TYPE_FLOAT:
			case TRX_TYPE_BLOB:
			case TRX_TYPE_UINT:
			case TRX_TYPE_ID:
				trx_strcpy_alloc(sql, sql_alloc, sql_offset, " not null");
				break;
			default:	/* TRX_TYPE_CHAR, TRX_TYPE_TEXT, TRX_TYPE_SHORTTEXT or TRX_TYPE_LONGTEXT */
				/* nothing to do */;
		}
#else
		trx_strcpy_alloc(sql, sql_alloc, sql_offset, " not null");
#endif
	}
}

static void	DBcreate_table_sql(char **sql, size_t *sql_alloc, size_t *sql_offset, const TRX_TABLE *table)
{
	int	i;

	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "create table %s (\n", table->table);

	for (i = 0; NULL != table->fields[i].name; i++)
	{
		if (0 != i)
			trx_strcpy_alloc(sql, sql_alloc, sql_offset, ",\n");
		DBfield_definition_string(sql, sql_alloc, sql_offset, &table->fields[i]);
	}
	if ('\0' != *table->recid)
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, ",\nprimary key (%s)", table->recid);

	trx_strcpy_alloc(sql, sql_alloc, sql_offset, "\n)" TRX_DB_TABLE_OPTIONS);
}

static void	DBrename_table_sql(char **sql, size_t *sql_alloc, size_t *sql_offset, const char *table_name,
		const char *new_name)
{
#ifdef HAVE_IBM_DB2
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "rename table " TRX_FS_SQL_NAME " to " TRX_FS_SQL_NAME,
			table_name, new_name);
#else
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table " TRX_FS_SQL_NAME " rename to " TRX_FS_SQL_NAME,
			table_name, new_name);
#endif
}

static void	DBdrop_table_sql(char **sql, size_t *sql_alloc, size_t *sql_offset, const char *table_name)
{
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "drop table %s", table_name);
}

static void	DBset_default_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, const TRX_FIELD *field)
{
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table %s" TRX_DB_ALTER_COLUMN " ", table_name);

#if defined(HAVE_MYSQL)
	DBfield_definition_string(sql, sql_alloc, sql_offset, field);
#elif defined(HAVE_ORACLE)
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s default '%s'", field->name, field->default_value);
#else
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s set default '%s'", field->name, field->default_value);
#endif
}

static void	DBmodify_field_type_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, const TRX_FIELD *field)
{
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table " TRX_FS_SQL_NAME TRX_DB_ALTER_COLUMN " ",
			table_name);

#ifdef HAVE_MYSQL
	DBfield_definition_string(sql, sql_alloc, sql_offset, field);
#else
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s" TRX_DB_SET_TYPE " ", field->name);
	DBfield_type_string(sql, sql_alloc, sql_offset, field);
#ifdef HAVE_POSTGRESQL
	if (NULL != field->default_value)
	{
		trx_strcpy_alloc(sql, sql_alloc, sql_offset, ";\n");
		DBset_default_sql(sql, sql_alloc, sql_offset, table_name, field);
	}
#endif
#endif
}

static void	DBdrop_not_null_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, const TRX_FIELD *field)
{
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table %s" TRX_DB_ALTER_COLUMN " ", table_name);

#if defined(HAVE_MYSQL)
	DBfield_definition_string(sql, sql_alloc, sql_offset, field);
#elif defined(HAVE_ORACLE)
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s null", field->name);
#else
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s drop not null", field->name);
#endif
}

static void	DBset_not_null_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, const TRX_FIELD *field)
{
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table %s" TRX_DB_ALTER_COLUMN " ", table_name);

#if defined(HAVE_MYSQL)
	DBfield_definition_string(sql, sql_alloc, sql_offset, field);
#elif defined(HAVE_ORACLE)
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s not null", field->name);
#else
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s set not null", field->name);
#endif
}

static void	DBadd_field_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, const TRX_FIELD *field)
{
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table " TRX_FS_SQL_NAME " add ", table_name);
	DBfield_definition_string(sql, sql_alloc, sql_offset, field);
}

static void	DBrename_field_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, const char *field_name, const TRX_FIELD *field)
{
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table " TRX_FS_SQL_NAME " ", table_name);

#ifdef HAVE_MYSQL
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "change column " TRX_FS_SQL_NAME " ", field_name);
	DBfield_definition_string(sql, sql_alloc, sql_offset, field);
#else
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "rename column " TRX_FS_SQL_NAME " to " TRX_FS_SQL_NAME,
			field_name, field->name);
#endif
}

static void	DBdrop_field_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, const char *field_name)
{
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table %s drop column %s", table_name, field_name);
}

static void	DBcreate_index_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, const char *index_name, const char *fields, int unique)
{
	trx_strcpy_alloc(sql, sql_alloc, sql_offset, "create");
	if (0 != unique)
		trx_strcpy_alloc(sql, sql_alloc, sql_offset, " unique");
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, " index %s on %s (%s)", index_name, table_name, fields);
}

static void	DBdrop_index_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, const char *index_name)
{
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "drop index %s", index_name);
#ifdef HAVE_MYSQL
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, " on %s", table_name);
#else
	TRX_UNUSED(table_name);
#endif
}

static void	DBrename_index_sql(char **sql, size_t *sql_alloc, size_t *sql_offset, const char *table_name,
		const char *old_name, const char *new_name, const char *fields, int unique)
{
#if defined(HAVE_IBM_DB2)
	TRX_UNUSED(table_name);
	TRX_UNUSED(fields);
	TRX_UNUSED(unique);
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "rename index %s to %s", old_name, new_name);
#elif defined(HAVE_MYSQL)
	DBcreate_index_sql(sql, sql_alloc, sql_offset, table_name, new_name, fields, unique);
	trx_strcpy_alloc(sql, sql_alloc, sql_offset, ";\n");
	DBdrop_index_sql(sql, sql_alloc, sql_offset, table_name, old_name);
	trx_strcpy_alloc(sql, sql_alloc, sql_offset, ";\n");
#elif defined(HAVE_ORACLE) || defined(HAVE_POSTGRESQL)
	TRX_UNUSED(table_name);
	TRX_UNUSED(fields);
	TRX_UNUSED(unique);
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter index %s rename to %s", old_name, new_name);
#endif
}

static void	DBadd_foreign_key_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, int id, const TRX_FIELD *field)
{
	trx_snprintf_alloc(sql, sql_alloc, sql_offset,
			"alter table " TRX_FS_SQL_NAME " add constraint c_%s_%d foreign key (" TRX_FS_SQL_NAME ")"
					" references " TRX_FS_SQL_NAME " (" TRX_FS_SQL_NAME ")", table_name, table_name,
					id, field->name, field->fk_table, field->fk_field);
	if (0 != (field->fk_flags & TRX_FK_CASCADE_DELETE))
		trx_strcpy_alloc(sql, sql_alloc, sql_offset, " on delete cascade");
}

static void	DBdrop_foreign_key_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, int id)
{
	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table %s" TRX_DROP_FK " c_%s_%d",
			table_name, table_name, id);
}

static int	DBreorg_table(const char *table_name)
{
#ifdef HAVE_IBM_DB2
	if (TRX_DB_OK <= DBexecute("call sysproc.admin_cmd ('reorg table %s')", table_name))
		return SUCCEED;

	return FAIL;
#else
	TRX_UNUSED(table_name);
	return SUCCEED;
#endif
}

int	DBcreate_table(const TRX_TABLE *table)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBcreate_table_sql(&sql, &sql_alloc, &sql_offset, table);

	if (TRX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	trx_free(sql);

	return ret;
}

int	DBrename_table(const char *table_name, const char *new_name)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBrename_table_sql(&sql, &sql_alloc, &sql_offset, table_name, new_name);

	if (TRX_DB_OK <= DBexecute("%s", sql))
		ret = DBreorg_table(new_name);

	trx_free(sql);

	return ret;
}

int	DBdrop_table(const char *table_name)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBdrop_table_sql(&sql, &sql_alloc, &sql_offset, table_name);

	if (TRX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	trx_free(sql);

	return ret;
}

int	DBadd_field(const char *table_name, const TRX_FIELD *field)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBadd_field_sql(&sql, &sql_alloc, &sql_offset, table_name, field);

	if (TRX_DB_OK <= DBexecute("%s", sql))
		ret = DBreorg_table(table_name);

	trx_free(sql);

	return ret;
}

int	DBrename_field(const char *table_name, const char *field_name, const TRX_FIELD *field)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBrename_field_sql(&sql, &sql_alloc, &sql_offset, table_name, field_name, field);

	if (TRX_DB_OK <= DBexecute("%s", sql))
		ret = DBreorg_table(table_name);

	trx_free(sql);

	return ret;
}

#ifdef HAVE_ORACLE
static int	DBmodify_field_type_with_copy(const char *table_name, const TRX_FIELD *field)
{
#define TRX_OLD_FIELD	"trx_old_tmp"

	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "alter table %s rename column %s to " TRX_OLD_FIELD,
			table_name, field->name);

	if (TRX_DB_OK > DBexecute("%s", sql))
		goto out;

	if (TRX_DB_OK > DBadd_field(table_name, field))
		goto out;

	sql_offset = 0;
	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update %s set %s=" TRX_OLD_FIELD, table_name,
			field->name);

	if (TRX_DB_OK > DBexecute("%s", sql))
		goto out;

	ret = DBdrop_field(table_name, TRX_OLD_FIELD);
out:
	trx_free(sql);

	return ret;

#undef TRX_OLD_FIELD
}
#endif

int	DBmodify_field_type(const char *table_name, const TRX_FIELD *field, const TRX_FIELD *old_field)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

#ifndef HAVE_ORACLE
	TRX_UNUSED(old_field);
#else
	/* Oracle cannot change column type in a general case if column contents are not null. Conversions like   */
	/* number -> nvarchar2 need special processing. New column is created with desired datatype and data from */
	/* old column is copied there. Then old column is dropped. This method does not preserve column order.    */
	/* NOTE: Existing column indexes and constraints are not respected by the current implementation!         */

	if (NULL != old_field && trx_oracle_column_type(old_field->type) != trx_oracle_column_type(field->type))
		return DBmodify_field_type_with_copy(table_name, field);
#endif
	DBmodify_field_type_sql(&sql, &sql_alloc, &sql_offset, table_name, field);

	if (TRX_DB_OK <= DBexecute("%s", sql))
		ret = DBreorg_table(table_name);

	trx_free(sql);

	return ret;
}

int	DBset_not_null(const char *table_name, const TRX_FIELD *field)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBset_not_null_sql(&sql, &sql_alloc, &sql_offset, table_name, field);

	if (TRX_DB_OK <= DBexecute("%s", sql))
		ret = DBreorg_table(table_name);

	trx_free(sql);

	return ret;
}

int	DBset_default(const char *table_name, const TRX_FIELD *field)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBset_default_sql(&sql, &sql_alloc, &sql_offset, table_name, field);

	if (TRX_DB_OK <= DBexecute("%s", sql))
		ret = DBreorg_table(table_name);

	trx_free(sql);

	return ret;
}

int	DBdrop_not_null(const char *table_name, const TRX_FIELD *field)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBdrop_not_null_sql(&sql, &sql_alloc, &sql_offset, table_name, field);

	if (TRX_DB_OK <= DBexecute("%s", sql))
		ret = DBreorg_table(table_name);

	trx_free(sql);

	return ret;
}

int	DBdrop_field(const char *table_name, const char *field_name)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBdrop_field_sql(&sql, &sql_alloc, &sql_offset, table_name, field_name);

	if (TRX_DB_OK <= DBexecute("%s", sql))
		ret = DBreorg_table(table_name);

	trx_free(sql);

	return ret;
}

int	DBcreate_index(const char *table_name, const char *index_name, const char *fields, int unique)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBcreate_index_sql(&sql, &sql_alloc, &sql_offset, table_name, index_name, fields, unique);

	if (TRX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	trx_free(sql);

	return ret;
}

int	DBdrop_index(const char *table_name, const char *index_name)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBdrop_index_sql(&sql, &sql_alloc, &sql_offset, table_name, index_name);

	if (TRX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	trx_free(sql);

	return ret;
}

int	DBrename_index(const char *table_name, const char *old_name, const char *new_name, const char *fields,
				int unique)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBrename_index_sql(&sql, &sql_alloc, &sql_offset, table_name, old_name, new_name, fields, unique);

	if (TRX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	trx_free(sql);

	return ret;
}

int	DBadd_foreign_key(const char *table_name, int id, const TRX_FIELD *field)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBadd_foreign_key_sql(&sql, &sql_alloc, &sql_offset, table_name, id, field);

	if (TRX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	trx_free(sql);

	return ret;
}

int	DBdrop_foreign_key(const char *table_name, int id)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBdrop_foreign_key_sql(&sql, &sql_alloc, &sql_offset, table_name, id);

	if (TRX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	trx_free(sql);

	return ret;
}

static int	DBcreate_dbversion_table(void)
{
	const TRX_TABLE	table =
			{"dbversion", "", 0,
				{
					{"mandatory", "0", NULL, NULL, 0, TRX_TYPE_INT, TRX_NOTNULL, 0},
					{"optional", "0", NULL, NULL, 0, TRX_TYPE_INT, TRX_NOTNULL, 0},
					{NULL}
				},
				NULL
			};
	int		ret;

	DBbegin();
	if (SUCCEED == (ret = DBcreate_table(&table)))
	{
		if (TRX_DB_OK > DBexecute("insert into dbversion (mandatory,optional) values (%d,%d)",
				TRX_FIRST_DB_VERSION, TRX_FIRST_DB_VERSION))
		{
			ret = FAIL;
		}
	}

	return DBend(ret);
}

static int	DBset_version(int version, unsigned char mandatory)
{
	char	sql[64];
	size_t	offset;

	offset = trx_snprintf(sql, sizeof(sql),  "update dbversion set ");
	if (0 != mandatory)
		offset += trx_snprintf(sql + offset, sizeof(sql) - offset, "mandatory=%d,", version);
	trx_snprintf(sql + offset, sizeof(sql) - offset, "optional=%d", version);

	if (TRX_DB_OK <= DBexecute("%s", sql))
		return SUCCEED;

	return FAIL;
}

#endif	/* not HAVE_SQLITE3 */

extern trx_dbpatch_t	DBPATCH_VERSION(2010)[];
extern trx_dbpatch_t	DBPATCH_VERSION(2020)[];
extern trx_dbpatch_t	DBPATCH_VERSION(2030)[];
extern trx_dbpatch_t	DBPATCH_VERSION(2040)[];
extern trx_dbpatch_t	DBPATCH_VERSION(2050)[];
extern trx_dbpatch_t	DBPATCH_VERSION(3000)[];
extern trx_dbpatch_t	DBPATCH_VERSION(3010)[];
extern trx_dbpatch_t	DBPATCH_VERSION(3020)[];
extern trx_dbpatch_t	DBPATCH_VERSION(3030)[];
extern trx_dbpatch_t	DBPATCH_VERSION(3040)[];
extern trx_dbpatch_t	DBPATCH_VERSION(3050)[];
extern trx_dbpatch_t	DBPATCH_VERSION(4000)[];
extern trx_dbpatch_t	DBPATCH_VERSION(4010)[];
extern trx_dbpatch_t	DBPATCH_VERSION(4020)[];
extern trx_dbpatch_t	DBPATCH_VERSION(4030)[];
extern trx_dbpatch_t	DBPATCH_VERSION(4040)[];

static trx_db_version_t dbversions[] = {
	{DBPATCH_VERSION(2010), "2.2 development"},
	{DBPATCH_VERSION(2020), "2.2 maintenance"},
	{DBPATCH_VERSION(2030), "2.4 development"},
	{DBPATCH_VERSION(2040), "2.4 maintenance"},
	{DBPATCH_VERSION(2050), "3.0 development"},
	{DBPATCH_VERSION(3000), "3.0 maintenance"},
	{DBPATCH_VERSION(3010), "3.2 development"},
	{DBPATCH_VERSION(3020), "3.2 maintenance"},
	{DBPATCH_VERSION(3030), "3.4 development"},
	{DBPATCH_VERSION(3040), "3.4 maintenance"},
	{DBPATCH_VERSION(3050), "4.0 development"},
	{DBPATCH_VERSION(4000), "4.0 maintenance"},
	{DBPATCH_VERSION(4010), "4.2 development"},
	{DBPATCH_VERSION(4020), "4.2 maintenance"},
	{DBPATCH_VERSION(4030), "4.4 development"},
	{DBPATCH_VERSION(4040), "4.4 maintenance"},
	{NULL}
};

static void	DBget_version(int *mandatory, int *optional)
{
	DB_RESULT	result;
	DB_ROW		row;

	*mandatory = -1;
	*optional = -1;

	result = DBselect("select mandatory,optional from dbversion");

	if (NULL != (row = DBfetch(result)))
	{
		*mandatory = atoi(row[0]);
		*optional = atoi(row[1]);
	}
	DBfree_result(result);

	if (-1 == *mandatory)
	{
		treegix_log(LOG_LEVEL_CRIT, "Cannot get the database version. Exiting ...");
		exit(EXIT_FAILURE);
	}
}

int	DBcheck_version(void)
{
	const char		*dbversion_table_name = "dbversion";
	int			db_mandatory, db_optional, required, ret = FAIL, i;
	trx_db_version_t	*dbversion;
	trx_dbpatch_t		*patches;

#ifndef HAVE_SQLITE3
	int			total = 0, current = 0, completed, last_completed = -1, optional_num = 0;
#endif
	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	required = TRX_FIRST_DB_VERSION;

	/* find out the required version number by getting the last mandatory version */
	/* of the last version patch array                                            */
	for (dbversion = dbversions; NULL != dbversion->patches; dbversion++)
		;

	patches = (--dbversion)->patches;

	for (i = 0; 0 != patches[i].version; i++)
	{
		if (0 != patches[i].mandatory)
			required = patches[i].version;
	}

	DBconnect(TRX_DB_CONNECT_NORMAL);

	if (SUCCEED != DBtable_exists(dbversion_table_name))
	{
#ifndef HAVE_SQLITE3
		treegix_log(LOG_LEVEL_DEBUG, "%s() \"%s\" does not exist", __func__, dbversion_table_name);

		if (SUCCEED != DBfield_exists("config", "server_check_interval"))
		{
			treegix_log(LOG_LEVEL_CRIT, "Cannot upgrade database: the database must"
					" correspond to version 2.0 or later. Exiting ...");
			goto out;
		}

		if (SUCCEED != DBcreate_dbversion_table())
			goto out;
#else
		treegix_log(LOG_LEVEL_CRIT, "The %s does not match Treegix database."
				" Current database version (mandatory/optional): UNKNOWN."
				" Required mandatory version: %08d.",
				get_program_type_string(program_type), required);
		treegix_log(LOG_LEVEL_CRIT, "Treegix does not support SQLite3 database upgrade.");

		goto out;
#endif
	}

	DBget_version(&db_mandatory, &db_optional);

#ifndef HAVE_SQLITE3
	for (dbversion = dbversions; NULL != (patches = dbversion->patches); dbversion++)
	{
		for (i = 0; 0 != patches[i].version; i++)
		{
			if (0 != patches[i].mandatory)
				optional_num = 0;
			else
				optional_num++;

			if (db_optional < patches[i].version)
				total++;
		}
	}

	if (required < db_mandatory)
#else
	if (required != db_mandatory)
#endif
	{
		treegix_log(LOG_LEVEL_CRIT, "The %s does not match Treegix database."
				" Current database version (mandatory/optional): %08d/%08d."
				" Required mandatory version: %08d.",
				get_program_type_string(program_type), db_mandatory, db_optional, required);
#ifdef HAVE_SQLITE3
		if (required > db_mandatory)
			treegix_log(LOG_LEVEL_CRIT, "Treegix does not support SQLite3 database upgrade.");
#endif
		goto out;
	}

	treegix_log(LOG_LEVEL_INFORMATION, "current database version (mandatory/optional): %08d/%08d",
			db_mandatory, db_optional);
	treegix_log(LOG_LEVEL_INFORMATION, "required mandatory version: %08d", required);

	ret = SUCCEED;

#ifndef HAVE_SQLITE3
	if (0 == total)
		goto out;

	if (0 != optional_num)
		treegix_log(LOG_LEVEL_INFORMATION, "optional patches were found");

	treegix_log(LOG_LEVEL_WARNING, "starting automatic database upgrade");

	for (dbversion = dbversions; NULL != dbversion->patches; dbversion++)
	{
		patches = dbversion->patches;

		for (i = 0; 0 != patches[i].version; i++)
		{
			static sigset_t	orig_mask, mask;

			if (db_optional >= patches[i].version)
				continue;

			/* block signals to prevent interruption of statements that cause an implicit commit */
			sigemptyset(&mask);
			sigaddset(&mask, SIGTERM);
			sigaddset(&mask, SIGINT);
			sigaddset(&mask, SIGQUIT);

			if (0 > sigprocmask(SIG_BLOCK, &mask, &orig_mask))
				treegix_log(LOG_LEVEL_WARNING, "cannot set sigprocmask to block the user signal");

			DBbegin();

			/* skipping the duplicated patches */
			if ((0 != patches[i].duplicates && patches[i].duplicates <= db_optional) ||
					SUCCEED == (ret = patches[i].function()))
			{
				ret = DBset_version(patches[i].version, patches[i].mandatory);
			}

			ret = DBend(ret);

			if (0 > sigprocmask(SIG_SETMASK, &orig_mask, NULL))
				treegix_log(LOG_LEVEL_WARNING,"cannot restore sigprocmask");

			if (SUCCEED != ret)
				break;

			current++;
			completed = (int)(100.0 * current / total);

			if (last_completed != completed)
			{
				treegix_log(LOG_LEVEL_WARNING, "completed %d%% of database upgrade", completed);
				last_completed = completed;
			}
		}

		if (SUCCEED != ret)
			break;
	}

	if (SUCCEED == ret)
		treegix_log(LOG_LEVEL_WARNING, "database upgrade fully completed");
	else
		treegix_log(LOG_LEVEL_CRIT, "database upgrade failed");
#endif	/* not HAVE_SQLITE3 */

out:
	DBclose();

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}
