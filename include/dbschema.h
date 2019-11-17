
#ifndef TREEGIX_DBSCHEMA_H
#define TREEGIX_DBSCHEMA_H

/* flags */
#define TRX_NOTNULL		0x01
#define TRX_PROXY		0x02

/* FK flags */
#define TRX_FK_CASCADE_DELETE	0x01

/* field types */
#define	TRX_TYPE_INT		0
#define	TRX_TYPE_CHAR		1
#define	TRX_TYPE_FLOAT		2
#define	TRX_TYPE_BLOB		3
#define	TRX_TYPE_TEXT		4
#define	TRX_TYPE_UINT		5
#define	TRX_TYPE_ID		6
#define	TRX_TYPE_SHORTTEXT	7
#define	TRX_TYPE_LONGTEXT	8

#define TRX_MAX_FIELDS		73 /* maximum number of fields in a table plus one for null terminator in dbschema.c */
#define TRX_TABLENAME_LEN	26
#define TRX_TABLENAME_LEN_MAX	(TRX_TABLENAME_LEN + 1)
#define TRX_FIELDNAME_LEN	28
#define TRX_FIELDNAME_LEN_MAX	(TRX_FIELDNAME_LEN + 1)

typedef struct
{
	const char	*name;
	const char	*default_value;
	const char	*fk_table;
	const char	*fk_field;
	unsigned short	length;
	unsigned char	type;
	unsigned char	flags;
	unsigned char	fk_flags;
}
TRX_FIELD;

typedef struct
{
	const char    	*table;
	const char	*recid;
	unsigned char	flags;
	TRX_FIELD	fields[TRX_MAX_FIELDS];
	const char	*uniq;
}
TRX_TABLE;

extern const TRX_TABLE	tables[];
extern const char	*const db_schema;
extern const char	*const db_schema_fkeys[];
extern const char	*const db_schema_fkeys_drop[];

#endif
