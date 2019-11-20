

#ifndef TREEGIX_DBUPGRADE_H
#define TREEGIX_DBUPGRADE_H

typedef struct
{
	int		(*function)(void);
	int		version;
	int		duplicates;
	unsigned char	mandatory;
}
trx_dbpatch_t;

#define DBPATCH_VERSION(treegix_version)			trx_dbpatches_##treegix_version

#define DBPATCH_START(treegix_version)			trx_dbpatch_t	DBPATCH_VERSION(treegix_version)[] = {
#define DBPATCH_END()					{NULL}};

#ifdef HAVE_SQLITE3

#define DBPATCH_ADD(version, duplicates, mandatory)	{NULL, version, duplicates, mandatory},

#else

#define DBPATCH_ADD(version, duplicates, mandatory)	{DBpatch_##version, version, duplicates, mandatory},

#ifdef HAVE_MYSQL
#define TRX_FS_SQL_NAME "`%s`"
#else
#define TRX_FS_SQL_NAME "%s"
#endif

int	DBcreate_table(const TRX_TABLE *table);
int	DBrename_table(const char *table_name, const char *new_name);
int	DBdrop_table(const char *table_name);
int	DBadd_field(const char *table_name, const TRX_FIELD *field);
int	DBrename_field(const char *table_name, const char *field_name, const TRX_FIELD *field);
int	DBmodify_field_type(const char *table_name, const TRX_FIELD *field, const TRX_FIELD *old_field);
int	DBset_not_null(const char *table_name, const TRX_FIELD *field);
int	DBset_default(const char *table_name, const TRX_FIELD *field);
int	DBdrop_not_null(const char *table_name, const TRX_FIELD *field);
int	DBdrop_field(const char *table_name, const char *field_name);
int	DBcreate_index(const char *table_name, const char *index_name, const char *fields, int unique);
int	DBdrop_index(const char *table_name, const char *index_name);
int	DBrename_index(const char *table_name, const char *old_name, const char *new_name, const char *fields,
		int unique);
int	DBadd_foreign_key(const char *table_name, int id, const TRX_FIELD *field);
int	DBdrop_foreign_key(const char *table_name, int id);

#endif

#endif
