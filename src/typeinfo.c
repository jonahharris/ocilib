/*
    +-----------------------------------------------------------------------------------------+
    |                                                                                         |
    |                               OCILIB - C Driver for Oracle                              |
    |                                                                                         |
    |                                (C Wrapper for Oracle OCI)                               |
    |                                                                                         |
    |                              Website : http://www.ocilib.net                            |
    |                                                                                         |
    |             Copyright (c) 2007-2015 Vincent ROGIER <vince.rogier@ocilib.net>            |
    |                                                                                         |
    +-----------------------------------------------------------------------------------------+
    |                                                                                         |
    |             This library is free software; you can redistribute it and/or               |
    |             modify it under the terms of the GNU Lesser General Public                  |
    |             License as published by the Free Software Foundation; either                |
    |             version 2 of the License, or (at your option) any later version.            |
    |                                                                                         |
    |             This library is distributed in the hope that it will be useful,             |
    |             but WITHOUT ANY WARRANTY; without even the implied warranty of              |
    |             MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU           |
    |             Lesser General Public License for more details.                             |
    |                                                                                         |
    |             You should have received a copy of the GNU Lesser General Public            |
    |             License along with this library; if not, write to the Free                  |
    |             Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.          |
    |                                                                                         |
    +-----------------------------------------------------------------------------------------+
*/

/* --------------------------------------------------------------------------------------------- *
 * $Id: typeinfo.c, Vincent Rogier $
 * --------------------------------------------------------------------------------------------- */

#include "ocilib_internal.h"

/* ********************************************************************************************* *
*                             PRIVATE VARIABLES
* ********************************************************************************************* */

static unsigned int TypeInfoTypeValues[] = { OCI_TIF_TABLE, OCI_TIF_VIEW, OCI_TIF_TYPE };

/* ********************************************************************************************* *
 *                             PRIVATE FUNCTIONS
 * ********************************************************************************************* */

/* --------------------------------------------------------------------------------------------- *
 * OCI_TypeInfoClose
 * --------------------------------------------------------------------------------------------- */

boolean OCI_TypeInfoClose
(
    OCI_TypeInfo *typinf
)
{
    ub2 i;

    OCI_CHECK(NULL == typinf, FALSE);

    for (i=0; i < typinf->nb_cols; i++)
    {
        OCI_FREE(typinf->cols[i].name)
    }

    OCI_FREE(typinf->cols)
    OCI_FREE(typinf->name)
    OCI_FREE(typinf->schema)
    OCI_FREE(typinf->offsets)

    return TRUE;
}

/* ********************************************************************************************* *
 *                            PUBLIC FUNCTIONS
 * ********************************************************************************************* */

/* --------------------------------------------------------------------------------------------- *
 * OCI_TypeInfoGet
 * --------------------------------------------------------------------------------------------- */

OCI_TypeInfo * OCI_API OCI_TypeInfoGet
(
    OCI_Connection *con,
    const otext    *name,
    unsigned int    type
)
{
    OCI_TypeInfo *typinf        = NULL;
    OCI_TypeInfo *syn_typinf    = NULL;
    OCI_Item *item              = NULL;
    OCIDescribe *dschp          = NULL;
    OCIParam *parmh1            = NULL;
    OCIParam *parmh2            = NULL;
    otext *str                  = NULL;
    int ptype                   = 0;
    ub1 desc_type               = 0;
    ub4 attr_type               = 0;
    ub4 num_type                = 0;
    boolean found               = FALSE;
    ub2 i;

    otext obj_schema[OCI_SIZE_OBJ_NAME + 1];
    otext obj_name[OCI_SIZE_OBJ_NAME + 1];

    OCI_LIB_CALL_ENTER(OCI_TypeInfo*, NULL)

    OCI_CHECK_PTR(OCI_IPC_CONNECTION, con)
    OCI_CHECK_PTR(OCI_IPC_STRING, name)
    OCI_CHECK_ENUM_VALUE(con, NULL, type, TypeInfoTypeValues, OTEXT("Type"))

    call_status = TRUE;

    obj_schema[0] = 0;
    obj_name[0]   = 0;

    /* is the schema provided in the object name ? */

    for (str = (otext *) name; *str; str++)
    {
        if (*str == OTEXT('.'))
        {
            ostrncat(obj_schema, name, str-name);
            ostrncat(obj_name, ++str, (size_t) OCI_SIZE_OBJ_NAME);
            break;
        }
    }

    /* if the schema is not provided, we just copy the object name */

    if (!obj_name[0])
    {
        ostrncat(obj_name, name, (size_t) OCI_SIZE_OBJ_NAME);
    }

    /* type name must be uppercase if not quoted */

    if (obj_name[0] != OTEXT('"'))
    {
        for (str = obj_name; *str; str++)
        {
            *str = (otext)otoupper(*str);
        }
    }

    /* schema name must be uppercase if not quoted */

    if (obj_schema[0] != OTEXT('"'))
    {
        for (str = obj_schema; *str; str++)
        {
            *str = (otext)otoupper(*str);
        }
    }    

    /* first try to find it in list */

    item = con->tinfs->head;

    /* walk along the list to find the type */

    while (item)
    {
        typinf = (OCI_TypeInfo *) item->data;

        if (typinf && (typinf->type == type))
        {
            if ((ostrcasecmp(typinf->name,   obj_name  ) == 0) &&
                (ostrcasecmp(typinf->schema, obj_schema) == 0))
            {
                found = TRUE;
                break;
            }
        }

        item = item->next;
    }

    /* Not found, so create type object */

    if (!found)
    {
        item = OCI_ListAppend(con->tinfs, sizeof(OCI_TypeInfo));

        call_status = (NULL != item);

        /* allocate describe handle */

        if (call_status)
        {
            typinf = (OCI_TypeInfo *) item->data;

            typinf->type        = type;
            typinf->con         = con;
            typinf->name        = ostrdup(obj_name);
            typinf->schema      = ostrdup(obj_schema);
            typinf->struct_size = 0;
            typinf->align       = 0;

            call_status = OCI_SUCCESSFUL(OCI_HandleAlloc(typinf->con->env,
                                                         (dvoid **) (void *) &dschp,
                                                         OCI_HTYPE_DESCRIBE, (size_t) 0,
                                                         (dvoid **) NULL));
                }

        /* perform describe */

        if (call_status)
        {
            otext buffer[(OCI_SIZE_OBJ_NAME * 2) + 2] = OTEXT("");

            size_t  size    = sizeof(buffer)/sizeof(otext);
            dbtext *dbstr1  = NULL;
            int     dbsize1 = -1;
            sb4     pbsp    = 1;

            str = buffer;

            /* compute full object name */

            if (typinf->schema && typinf->schema[0])
            {
                str   = ostrncat(buffer, typinf->schema, size);
                size -= ostrlen(typinf->schema);
                str   = ostrncat(str, OTEXT("."), size);
                size -= (size_t) 1;
            }

            ostrncat(str, typinf->name, size);

            dbstr1 = OCI_StringGetOracleString(str, &dbsize1);

            /* set public scope to include synonyms */
                
            OCI_CALL2
            (
                call_status, con,

                OCIAttrSet(dschp, OCI_HTYPE_DESCRIBE, &pbsp, (ub4) sizeof(pbsp), 
                            OCI_ATTR_DESC_PUBLIC, con->err)
            )

            /* describe call */

            OCI_CALL2
            (
                call_status, con,

                OCIDescribeAny(con->cxt, con->err, (dvoid *) dbstr1,
                                (ub4) dbsize1, OCI_OTYPE_NAME,
                                OCI_DEFAULT, OCI_PTYPE_UNK, dschp)
            )

            OCI_StringReleaseOracleString(dbstr1);

            /* get parameter handle */
                
            OCI_CALL2
            (
                call_status, con,

                OCIAttrGet(dschp, OCI_HTYPE_DESCRIBE, &parmh1,
                            NULL, OCI_ATTR_PARAM, con->err)
            )
            
            /* get object type */
                
            OCI_CALL2
            (
                call_status, con,

                OCIAttrGet(parmh1, OCI_DTYPE_PARAM, &desc_type,
                           NULL, OCI_ATTR_PTYPE, con->err)
            )
        }

        /* on successful describe call, retrieve all information about the object 
           if it is not a synonym */

        if (call_status)
        {
            switch (desc_type)
            {
                case OCI_PTYPE_TYPE:
                {
                    if (OCI_UNKNOWN != typinf->type)
                    {
                        call_status = (OCI_TIF_TYPE == typinf->type);
                    }

                    typinf->type = OCI_TIF_TYPE;
                    
                    if (call_status)
                    {
                        boolean pdt = FALSE;
                        OCIRef *ref = NULL;

                        attr_type = OCI_ATTR_LIST_TYPE_ATTRS;
                        num_type  = OCI_ATTR_NUM_TYPE_ATTRS;
                        ptype     = OCI_DESC_TYPE;

                        /* get the object type descriptor */

                        OCI_CALL2
                        (
                            call_status, con,

                            OCIAttrGet(parmh1, OCI_DTYPE_PARAM, &ref,
                            NULL, OCI_ATTR_REF_TDO, con->err)
                        )

                        OCI_CALL2
                        (
                            call_status, con,

                            OCITypeByRef(typinf->con->env, con->err, ref,
                            OCI_DURATION_SESSION, OCI_TYPEGET_ALL,    &typinf->tdo)
                        )
                    
                        /* check if it's system predefined type if order to avoid the next call
                           that is not allowed on system types */

                        OCI_CALL2
                        (
                            call_status, con,

                            OCIAttrGet(parmh1, OCI_DTYPE_PARAM, &pdt,
                                       NULL, OCI_ATTR_IS_PREDEFINED_TYPE, con->err)
                        )

                        if (!pdt)
                        {
                            OCI_CALL2
                            (
                                call_status, con,

                                OCIAttrGet(parmh1, OCI_DTYPE_PARAM, &typinf->typecode,
                                           NULL, OCI_ATTR_TYPECODE, con->err)
                            )
                        }
                    
                    }

                    break;
                }
                case OCI_PTYPE_TABLE:
                case OCI_PTYPE_VIEW:
            #if OCI_VERSION_COMPILE >= OCI_10_1
                case OCI_PTYPE_TABLE_ALIAS:
            #endif
                {
                    if (OCI_UNKNOWN != typinf->type)
                    {
                        call_status = (((OCI_TIF_TABLE == typinf->type) && (OCI_PTYPE_VIEW != desc_type)) ||
                                       ((OCI_TIF_VIEW  == typinf->type) && (OCI_PTYPE_VIEW == desc_type)));
                    }

                    typinf->type = (OCI_PTYPE_VIEW == desc_type ? OCI_TIF_VIEW : OCI_TIF_TABLE);
 
                    if (call_status)
                    {
                        attr_type = OCI_ATTR_LIST_COLUMNS;
                        num_type  = OCI_ATTR_NUM_COLS;
                        ptype     = OCI_DESC_TABLE;             
                    }
                    
                    break;
                }
                case OCI_PTYPE_SYN:
                {
                    otext *syn_schema_name   = NULL;
                    otext *syn_object_name   = NULL;
                    otext *syn_link_name     = NULL;

                    otext syn_fullname[(OCI_SIZE_OBJ_NAME * 3) + 3] = OTEXT("");

                    /* get link schema, object and database link names */

                    call_status = call_status && OCI_GetStringAttribute(con, parmh1, OCI_DTYPE_PARAM,
                                                                          OCI_ATTR_SCHEMA_NAME, 
                                                                          &syn_schema_name);
                    
                    call_status = call_status && OCI_GetStringAttribute(con, parmh1, OCI_DTYPE_PARAM,
                                                                          OCI_ATTR_NAME, 
                                                                          &syn_object_name);
 
                    call_status = call_status && OCI_GetStringAttribute(con, parmh1, OCI_DTYPE_PARAM,
                                                                         OCI_ATTR_LINK, &syn_link_name);

                    /* compute link full name */

                    OCI_StringGetFullTypeName(syn_schema_name, syn_object_name, syn_link_name, syn_fullname, (sizeof(syn_fullname) / sizeof(otext)) - 1);

                    /* retrieve the type info of the real object */

                    syn_typinf = OCI_TypeInfoGet (con, syn_fullname, typinf->type);
                         
                    /* free temporaRy strings */

                    OCI_MemFree (syn_link_name);
                    OCI_MemFree (syn_object_name);
                    OCI_MemFree (syn_schema_name);
                    
                    /* do we have a valid object ? */

                    call_status = (NULL != syn_typinf);

                    break;
                }
            }

            /*  did we handle a supported object other than a synonym */

            if (call_status && (OCI_UNKNOWN != ptype))
            {
                /* do we need get more attributes for collections ? */

                if (SQLT_NCO == typinf->typecode)
                {
                    typinf->nb_cols = 1;

                    ptype  = OCI_DESC_COLLECTION;
                    parmh2 = parmh1;

                    OCI_CALL2
                    (
                        call_status, con,

                        OCIAttrGet(parmh1, OCI_DTYPE_PARAM, &typinf->colcode,
                                   NULL, OCI_ATTR_COLLECTION_TYPECODE, con->err)
                    )
                }
                else
                {
                    OCI_CALL2
                    (
                        call_status, con,

                        OCIAttrGet(parmh1, OCI_DTYPE_PARAM, &parmh2,
                                   NULL, attr_type, con->err)
                    )

                    OCI_CALL2
                    (
                        call_status, con,

                        OCIAttrGet(parmh1, OCI_DTYPE_PARAM, &typinf->nb_cols,
                                   NULL, num_type, con->err)
                    )
                }

                /* allocates memory for cached offsets */

                if (typinf->nb_cols > 0)
                {
                    typinf->offsets = (int *) OCI_MemAlloc(OCI_IPC_ARRAY,
                                                           sizeof(*typinf->offsets),
                                                           (size_t) typinf->nb_cols,
                                                           FALSE);

                    call_status = (NULL != typinf->offsets);

                    if (call_status)
                    {
                        memset(typinf->offsets, -1, sizeof(*typinf->offsets) * typinf->nb_cols);
                    }
                }

                /* allocates memory for children */

                if (typinf->nb_cols > 0)
                {
                    typinf->cols = (OCI_Column *) OCI_MemAlloc(OCI_IPC_COLUMN,  sizeof(*typinf->cols),
                                                               (size_t) typinf->nb_cols, TRUE);

                    /* describe children */

                    if (typinf->cols)
                    {
                        for (i = 0; i < typinf->nb_cols; i++)
                        {
                            call_status = call_status && OCI_ColumnDescribe(&typinf->cols[i], con,
                                                                            NULL, parmh2, i + 1, ptype);

                            call_status = call_status && OCI_ColumnMap(&typinf->cols[i], NULL);

                            if (!call_status)
                            {
                                break;
                            }
                        }
                    }
                    else
                    {
                        call_status = FALSE;
                    }
                }
            }
        }
    }

    /* free describe handle */

    if (dschp)
    {
        OCI_HandleFree(dschp, OCI_HTYPE_DESCRIBE);
    }

    /* increment type info reference counter on success */

    if (typinf)
    {
        typinf->refcount++;

        /* type checking sanity checks */

        if ((type != OCI_UNKNOWN) && (typinf->type != type))
        {
            OCI_ExceptionTypeInfoWrongType(con, name);

            call_status = FALSE;
        }
    }

    /* handle errors */

    if (!call_status || syn_typinf)
    {
        OCI_TypeInfoFree(typinf);
        typinf = NULL;
    }


    if (call_status)
    {
        call_retval = syn_typinf ? syn_typinf : typinf;
    }

    OCI_LIB_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_TypeInfoFree
 * --------------------------------------------------------------------------------------------- */

boolean OCI_API OCI_TypeInfoFree
(
    OCI_TypeInfo *typinf
)
{
    OCI_LIB_CALL_ENTER(boolean, FALSE)

    OCI_CHECK_PTR(OCI_IPC_TYPE_INFO, typinf)

    typinf->refcount--;

    if (typinf->refcount == 0)
    {
        OCI_ListRemove(typinf->con->tinfs, typinf);

        OCI_TypeInfoClose(typinf);

        OCI_FREE(typinf)
    }

    call_retval = call_status = TRUE;

    OCI_LIB_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_TypeInfoGetType
 * --------------------------------------------------------------------------------------------- */

unsigned int OCI_API OCI_TypeInfoGetType
(
    OCI_TypeInfo *typinf
)
{
    OCI_LIB_CALL_ENTER(unsigned int, OCI_UNKNOWN)

    OCI_CHECK_PTR(OCI_IPC_TYPE_INFO, typinf)

    call_retval = typinf->type;
    call_status = TRUE;

    OCI_LIB_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_TypeInfoGetConnection
 * --------------------------------------------------------------------------------------------- */

OCI_Connection * OCI_API OCI_TypeInfoGetConnection
(
    OCI_TypeInfo *typinf
)
{
    OCI_LIB_CALL_ENTER(OCI_Connection*, NULL)

    OCI_CHECK_PTR(OCI_IPC_TYPE_INFO, typinf)

    call_retval =  typinf->con;
    call_status = TRUE;

    OCI_LIB_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_TypeInfoGetColumnCount
 * --------------------------------------------------------------------------------------------- */

unsigned int OCI_API OCI_TypeInfoGetColumnCount
(
    OCI_TypeInfo *typinf
)
{
    OCI_LIB_CALL_ENTER(unsigned int, 0)

    OCI_CHECK_PTR(OCI_IPC_TYPE_INFO, typinf)

    call_retval = typinf->nb_cols;
    call_status = TRUE;

    OCI_LIB_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_TypeInfoGetColumn
 * --------------------------------------------------------------------------------------------- */

OCI_Column * OCI_API OCI_TypeInfoGetColumn
(
    OCI_TypeInfo *typinf,
    unsigned int  index
)
{
    OCI_LIB_CALL_ENTER(OCI_Column *, NULL)

    OCI_CHECK_PTR(OCI_IPC_TYPE_INFO, typinf)
    OCI_CHECK_BOUND(typinf->con, index, 1,  typinf->nb_cols)

    call_retval = &(typinf->cols[index - 1]);
    call_status = TRUE;

    OCI_LIB_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_TypeInfoGetName
 * --------------------------------------------------------------------------------------------- */

const otext * OCI_API OCI_TypeInfoGetName
(
    OCI_TypeInfo *typinf
)
{
    OCI_LIB_CALL_ENTER(const otext*, NULL)

    OCI_CHECK_PTR(OCI_IPC_TYPE_INFO, typinf)

    call_retval = typinf->name;
    call_status = TRUE;

    OCI_LIB_CALL_EXIT()
}
