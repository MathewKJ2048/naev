/*
 * See Licensing and Copyright notice in naev.h
 */
/**
 * @file unidiff.c
 *
 * @brief Handles the application and removal of 'diffs' to the universe.
 *
 * Diffs allow changing spobs, factions, etc... in the universe.
 *  These are meant to be applied after the player triggers them, mostly
 *  through missions.
 */
/** @cond */
#include <stdlib.h>
/** @endcond */

#include "unidiff.h"

#include "array.h"
#include "conf.h"
#include "economy.h"
#include "log.h"
#include "map_overlay.h"
#include "ndata.h"
#include "nxml.h"
#include "player.h"
#include "safelanes.h"
#include "space.h"

/**
 * @brief Universe diff filepath list.
 */
typedef struct UniDiffData_ {
   char *name;     /**< Name of the diff (read from XML). */
   char *filename; /**< Filename of the diff. */
} UniDiffData_t;
static UniDiffData_t *diff_available = NULL; /**< Available diffs. */

/**
 * @struct UniDiff_t
 *
 * @brief Represents each Universe Diff.
 */
typedef struct UniDiff_ {
   char      *name;    /**< Name of the diff. */
   UniHunk_t *applied; /**< Applied hunks. */
   UniHunk_t *failed;  /**< Failed hunks. */
} UniDiff_t;

#define HUNK_CUST( STR, TYPE, DTYPE, FUNC )                                    \
   if ( xml_isNode( cur, STR ) ) {                                             \
      hunk.target.type   = base.target.type;                                   \
      hunk.target.u.name = strdup( base.target.u.name );                       \
      hunk.type          = TYPE;                                               \
      hunk.dtype         = DTYPE;                                              \
      FUNC if ( diff_patchHunk( &hunk ) < 0 ) diff_hunkFailed( diff, &hunk );  \
      else diff_hunkSuccess( diff, &hunk );                                    \
      continue;                                                                \
   }
#define HUNK_NONE( STR, TYPE )                                                 \
   HUNK_CUST( STR, TYPE, HUNK_DATA_NONE, hunk.u.name = NULL; );
#define HUNK_STRD( STR, TYPE )                                                 \
   HUNK_CUST( STR, TYPE, HUNK_DATA_STRING, hunk.u.name = xml_getStrd( cur ); );
#define HUNK_UINT( STR, TYPE )                                                 \
   HUNK_CUST( STR, TYPE, HUNK_DATA_INT, hunk.u.data = xml_getUInt( cur ); );
#define HUNK_FLOAT( STR, TYPE )                                                \
   HUNK_CUST( STR, TYPE, HUNK_DATA_FLOAT, hunk.u.fdata = xml_getFloat( cur ); );

/*
 * Diff stack.
 */
static UniDiff_t *diff_stack = NULL; /**< Currently applied universe diffs. */

/* Useful variables. */
static int diff_universe_changed =
   0; /**< Whether or not the universe changed. */
static int         diff_universe_defer = 0; /**< Defers changes to later. */
static const char *diff_nav_spob =
   NULL; /**< Stores the player's spob target if necessary. */
static const char *diff_nav_hyperspace =
   NULL; /**< Stores the player's hyperspace target if necessary. */

static const char *hunk_name[HUNK_TYPE_SENTINAL] = {
   [HUNK_TYPE_NONE]                    = N_( "none" ),
   [HUNK_TYPE_SPOB_ADD]                = N_( "spob add" ),
   [HUNK_TYPE_SPOB_REMOVE]             = N_( "spob remove" ),
   [HUNK_TYPE_VSPOB_ADD]               = N_( "virtual spob add" ),
   [HUNK_TYPE_VSPOB_REMOVE]            = N_( "virtual spob remove" ),
   [HUNK_TYPE_JUMP_ADD]                = N_( "jump add" ),
   [HUNK_TYPE_JUMP_REMOVE]             = N_( "jump remove" ),
   [HUNK_TYPE_TECH_ADD]                = N_( "tech add" ),
   [HUNK_TYPE_TECH_REMOVE]             = N_( "tech remove" ),
   [HUNK_TYPE_SSYS_BACKGROUND]         = N_( "ssys background" ),
   [HUNK_TYPE_SSYS_BACKGROUND_REVERT]  = N_( "ssys background revert" ),
   [HUNK_TYPE_SSYS_FEATURES]           = N_( "ssys features" ),
   [HUNK_TYPE_SSYS_FEATURES_REVERT]    = N_( "ssys features revert" ),
   [HUNK_TYPE_SPOB_FACTION]            = N_( "spob faction" ),
   [HUNK_TYPE_SPOB_FACTION_REMOVE]     = N_( "spob faction removal" ),
   [HUNK_TYPE_SPOB_POPULATION]         = N_( "spob population" ),
   [HUNK_TYPE_SPOB_POPULATION_REMOVE]  = N_( "spob population removal" ),
   [HUNK_TYPE_SPOB_DISPLAYNAME]        = N_( "spob displayname" ),
   [HUNK_TYPE_SPOB_DISPLAYNAME_REVERT] = N_( "spob displayname revert" ),
   [HUNK_TYPE_SPOB_DESCRIPTION]        = N_( "spob description" ),
   [HUNK_TYPE_SPOB_DESCRIPTION_REVERT] = N_( "spob description revert" ),
   [HUNK_TYPE_SPOB_BAR]                = N_( "spob bar" ),
   [HUNK_TYPE_SPOB_BAR_REVERT]         = N_( "spob bar revert" ),
   [HUNK_TYPE_SPOB_SPACE]              = N_( "spob space" ),
   [HUNK_TYPE_SPOB_SPACE_REVERT]       = N_( "spob space revert" ),
   [HUNK_TYPE_SPOB_EXTERIOR]           = N_( "spob exterior" ),
   [HUNK_TYPE_SPOB_EXTERIOR_REVERT]    = N_( "spob exterior revert" ),
   [HUNK_TYPE_SPOB_LUA]                = N_( "spob lua" ),
   [HUNK_TYPE_SPOB_LUA_REVERT]         = N_( "spob lua revert" ),
   [HUNK_TYPE_SPOB_SERVICE_ADD]        = N_( "spob service add" ),
   [HUNK_TYPE_SPOB_SERVICE_REMOVE]     = N_( "spob service remove" ),
   [HUNK_TYPE_SPOB_NOMISNSPAWN_ADD]    = N_( "spob nomissionspawn add" ),
   [HUNK_TYPE_SPOB_NOMISNSPAWN_REMOVE] = N_( "spob nomissionspawn remove" ),
   [HUNK_TYPE_SPOB_TECH_ADD]           = N_( "spob tech add" ),
   [HUNK_TYPE_SPOB_TECH_REMOVE]        = N_( "spob tech remove" ),
   [HUNK_TYPE_SPOB_TAG_ADD]            = N_( "spob tech add" ),
   [HUNK_TYPE_SPOB_TAG_REMOVE]         = N_( "spob tech remove" ),
   [HUNK_TYPE_FACTION_VISIBLE]         = N_( "faction visible" ),
   [HUNK_TYPE_FACTION_INVISIBLE]       = N_( "faction invisible" ),
   [HUNK_TYPE_FACTION_ALLY]            = N_( "faction set ally" ),
   [HUNK_TYPE_FACTION_ENEMY]           = N_( "faction set enemy" ),
   [HUNK_TYPE_FACTION_NEUTRAL]         = N_( "faction set neutral" ),
   [HUNK_TYPE_FACTION_REALIGN]         = N_( "faction alignment reset" ),
};
static UniHunkType_t hunk_reverse[HUNK_TYPE_SENTINAL] = {
   [HUNK_TYPE_NONE]                    = HUNK_TYPE_SENTINAL,
   [HUNK_TYPE_SPOB_ADD]                = HUNK_TYPE_SPOB_REMOVE,
   [HUNK_TYPE_SPOB_REMOVE]             = HUNK_TYPE_SPOB_ADD,
   [HUNK_TYPE_VSPOB_ADD]               = HUNK_TYPE_VSPOB_REMOVE,
   [HUNK_TYPE_VSPOB_REMOVE]            = HUNK_TYPE_VSPOB_ADD,
   [HUNK_TYPE_JUMP_ADD]                = HUNK_TYPE_JUMP_REMOVE,
   [HUNK_TYPE_JUMP_REMOVE]             = HUNK_TYPE_JUMP_ADD,
   [HUNK_TYPE_SSYS_BACKGROUND]         = HUNK_TYPE_SSYS_BACKGROUND_REVERT,
   [HUNK_TYPE_SSYS_FEATURES]           = HUNK_TYPE_SSYS_FEATURES_REVERT,
   [HUNK_TYPE_TECH_ADD]                = HUNK_TYPE_TECH_REMOVE,
   [HUNK_TYPE_TECH_REMOVE]             = HUNK_TYPE_TECH_ADD,
   [HUNK_TYPE_SPOB_FACTION]            = HUNK_TYPE_SPOB_FACTION_REMOVE,
   [HUNK_TYPE_SPOB_POPULATION]         = HUNK_TYPE_SPOB_POPULATION_REMOVE,
   [HUNK_TYPE_SPOB_DISPLAYNAME]        = HUNK_TYPE_SPOB_DISPLAYNAME_REVERT,
   [HUNK_TYPE_SPOB_DESCRIPTION]        = HUNK_TYPE_SPOB_DESCRIPTION_REVERT,
   [HUNK_TYPE_SPOB_BAR]                = HUNK_TYPE_SPOB_BAR_REVERT,
   [HUNK_TYPE_SPOB_SERVICE_ADD]        = HUNK_TYPE_SPOB_SERVICE_REMOVE,
   [HUNK_TYPE_SPOB_SERVICE_REMOVE]     = HUNK_TYPE_SPOB_SERVICE_ADD,
   [HUNK_TYPE_SPOB_NOMISNSPAWN_ADD]    = HUNK_TYPE_SPOB_NOMISNSPAWN_REMOVE,
   [HUNK_TYPE_SPOB_NOMISNSPAWN_REMOVE] = HUNK_TYPE_SPOB_NOMISNSPAWN_ADD,
   [HUNK_TYPE_SPOB_TECH_ADD]           = HUNK_TYPE_SPOB_TECH_REMOVE,
   [HUNK_TYPE_SPOB_TECH_REMOVE]        = HUNK_TYPE_SPOB_TECH_ADD,
   [HUNK_TYPE_SPOB_TAG_ADD]            = HUNK_TYPE_SPOB_TAG_REMOVE,
   [HUNK_TYPE_SPOB_TAG_REMOVE]         = HUNK_TYPE_SPOB_TAG_ADD,
   [HUNK_TYPE_SPOB_SPACE]              = HUNK_TYPE_SPOB_SPACE_REVERT,
   [HUNK_TYPE_SPOB_EXTERIOR]           = HUNK_TYPE_SPOB_EXTERIOR_REVERT,
   [HUNK_TYPE_SPOB_LUA]                = HUNK_TYPE_SPOB_LUA_REVERT,
   [HUNK_TYPE_FACTION_VISIBLE]         = HUNK_TYPE_FACTION_INVISIBLE,
   [HUNK_TYPE_FACTION_INVISIBLE]       = HUNK_TYPE_FACTION_VISIBLE,
   [HUNK_TYPE_FACTION_ALLY]            = HUNK_TYPE_FACTION_REALIGN,
   [HUNK_TYPE_FACTION_ENEMY]           = HUNK_TYPE_FACTION_REALIGN,
   [HUNK_TYPE_FACTION_NEUTRAL]         = HUNK_TYPE_FACTION_REALIGN,
};

/*
 * Prototypes.
 */
static int diff_applyInternal( const char *name, int oneshot );
NONNULL( 1 ) static UniDiff_t *diff_get( const char *name );
static UniDiff_t *diff_newDiff( void );
static int        diff_removeDiff( UniDiff_t *diff );
static int        diff_patchSystem( UniDiff_t *diff, xmlNodePtr node );
static int        diff_patchTech( UniDiff_t *diff, xmlNodePtr node );
static int        diff_patch( xmlNodePtr parent );
static int        diff_patchHunk( UniHunk_t *hunk );
static void       diff_hunkFailed( UniDiff_t *diff, const UniHunk_t *hunk );
static void       diff_hunkSuccess( UniDiff_t *diff, const UniHunk_t *hunk );
static void       diff_cleanup( UniDiff_t *diff );
/* Misc. */
static int diff_checkUpdateUniverse( void );
/* Externed. */
int diff_save( xmlTextWriterPtr writer ); /**< Used in save.c */
int diff_load( xmlNodePtr parent );       /**< Used in save.c */

/**
 * @brief Simple comparison for UniDiffData_t based on name.
 */
static int diff_cmp( const void *p1, const void *p2 )
{
   const UniDiffData_t *d1, *d2;
   d1 = (const UniDiffData_t *)p1;
   d2 = (const UniDiffData_t *)p2;
   return strcmp( d1->name, d2->name );
}

/**
 * @brief Loads available universe diffs.
 *
 *    @return 0 on success.
 */
int diff_init( void )
{
#if DEBUGGING
   Uint32 time = SDL_GetTicks();

   for ( int i = 0; i < HUNK_TYPE_SENTINAL; i++ ) {
      if ( hunk_name[i] == NULL )
         WARN( "HUNK_TYPE '%d' missing error message!", i );
      if ( hunk_reverse[i] == HUNK_TYPE_NONE ) {
         /* It's possible that this is an internal usage only reverse one, so we
          * have to see if something points to it instead. */
         int found = 0;
         for ( int j = 0; j < HUNK_TYPE_SENTINAL; j++ ) {
            if ( hunk_reverse[j] == (UniHunkType_t)i ) {
               found = 1;
               break;
            }
         }
         /* If not found, that means that the type is not referred to by anyone.
          */
         if ( !found )
            WARN( "HUNK_TYPE '%d' missing reverse!", i );
      }
   }
#endif /* DEBUGGING */

   char **diff_files = ndata_listRecursive( UNIDIFF_DATA_PATH );
   diff_available =
      array_create_size( UniDiffData_t, array_size( diff_files ) );
   for ( int i = 0; i < array_size( diff_files ); i++ ) {
      xmlDocPtr      doc;
      xmlNodePtr     node;
      UniDiffData_t *diff;

      /* Parse the header. */
      doc = xml_parsePhysFS( diff_files[i] );
      if ( doc == NULL ) {
         free( diff_files[i] );
         continue;
      }

      node = doc->xmlChildrenNode;
      if ( !xml_isNode( node, "unidiff" ) ) {
         WARN( _( "Malformed XML header for '%s' UniDiff: missing root element "
                  "'%s'" ),
               diff_files[i], "unidiff" );
         xmlFreeDoc( doc );
         free( diff_files[i] );
         continue;
      }

      diff           = &array_grow( &diff_available );
      diff->filename = diff_files[i];
      xmlr_attr_strd( node, "name", diff->name );
      xmlFreeDoc( doc );
   }
   array_free( diff_files );
   array_shrink( &diff_available );

   /* Sort and warn about duplicates. */
   qsort( diff_available, array_size( diff_available ), sizeof( UniDiffData_t ),
          diff_cmp );
   for ( int i = 0; i < array_size( diff_available ) - 1; i++ ) {
      UniDiffData_t       *d  = &diff_available[i];
      const UniDiffData_t *dn = &diff_available[i + 1];
      if ( strcmp( d->name, dn->name ) == 0 )
         WARN( _( "Two unidiff have the same name '%s'!" ), d->name );
   }

#if DEBUGGING
   if ( conf.devmode ) {
      DEBUG( n_( "Loaded %d UniDiff in %.3f s", "Loaded %d UniDiffs in %.3f s",
                 array_size( diff_available ) ),
             array_size( diff_available ), ( SDL_GetTicks() - time ) / 1000. );
   } else
      DEBUG( n_( "Loaded %d UniDiff", "Loaded %d UniDiffs",
                 array_size( diff_available ) ),
             array_size( diff_available ) );
#endif /* DEBUGGING */

   return 0;
}

void diff_exit( void )
{
}

/**
 * @brief Checks if a diff is currently applied.
 *
 *    @param name Diff to check.
 *    @return 0 if it's not applied, 1 if it is.
 */
int diff_isApplied( const char *name )
{
   if ( diff_get( name ) != NULL )
      return 1;
   return 0;
}

/**
 * @brief Gets a diff by name.
 *
 *    @param name Name of the diff to get.
 *    @return The diff if found or NULL if not found.
 */
static UniDiff_t *diff_get( const char *name )
{
   for ( int i = 0; i < array_size( diff_stack ); i++ )
      if ( strcmp( diff_stack[i].name, name ) == 0 )
         return &diff_stack[i];
   return NULL;
}

/**
 * @brief Applies a diff to the universe.
 *
 *    @param name Diff to apply.
 *    @return 0 on success.
 */
int diff_apply( const char *name )
{
   diff_nav_hyperspace = NULL;
   diff_nav_spob       = NULL;
   if ( player.p ) {
      if ( player.p->nav_hyperspace >= 0 )
         diff_nav_hyperspace =
            cur_system->jumps[player.p->nav_hyperspace].target->name;
      if ( player.p->nav_spob >= 0 )
         diff_nav_spob = cur_system->spobs[player.p->nav_spob]->name;
   }
   return diff_applyInternal( name, 1 );
}

/**
 * @brief Applies a diff to the universe.
 *
 *    @param name Diff to apply.
 *    @param oneshot Whether or not this diff should be applied as a single
 * one-shot diff.
 *    @return 0 on success.
 */
static int diff_applyInternal( const char *name, int oneshot )
{
   xmlNodePtr     node;
   xmlDocPtr      doc;
   UniDiffData_t *d;

   /* Check if already applied. */
   if ( diff_isApplied( name ) )
      return 0;

   /* Reset change variable. */
   if ( oneshot && !diff_universe_defer )
      diff_universe_changed = 0;

   const UniDiffData_t q = { .name = (char *)name };
   d = bsearch( &q, diff_available, array_size( diff_available ),
                sizeof( UniDiffData_t ), diff_cmp );
   if ( d == NULL ) {
      WARN( _( "UniDiff '%s' not found in %s!" ), name, UNIDIFF_DATA_PATH );
      return -1;
   }

   doc = xml_parsePhysFS( d->filename );

   node = doc->xmlChildrenNode;
   if ( strcmp( (char *)node->name, "unidiff" ) ) {
      ERR( _( "Malformed unidiff file: missing root element 'unidiff'" ) );
      return 0;
   }

   /* Apply it. */
   diff_patch( node );

   xmlFreeDoc( doc );

   /* Update universe. */
   if ( oneshot )
      diff_checkUpdateUniverse();

   return 0;
}

/**
 * @brief Patches a system.
 *
 *    @param diff Diff that is doing the patching.
 *    @param node Node containing the system.
 *    @return 0 on success.
 */
static int diff_patchSystem( UniDiff_t *diff, xmlNodePtr node )
{
   UniHunk_t  base, hunk;
   xmlNodePtr cur;
   char      *buf;

   /* Set the target. */
   memset( &base, 0, sizeof( UniHunk_t ) );
   base.target.type = HUNK_TARGET_SYSTEM;
   xmlr_attr_strd( node, "name", base.target.u.name );
   if ( base.target.u.name == NULL ) {
      WARN( _( "Unidiff '%s' has a system node without a 'name' tag, not "
               "applying." ),
            diff->name );
      return -1;
   }

   /* Now parse the possible changes. */
   cur = node->xmlChildrenNode;
   do {
      xml_onlyNodes( cur );
      if ( xml_isNode( cur, "spob" ) ) {
         buf = xml_get( cur );
         if ( buf == NULL ) {
            WARN( _( "Unidiff '%s': Null hunk type." ), diff->name );
            continue;
         }

         hunk.target.type   = base.target.type;
         hunk.target.u.name = strdup( base.target.u.name );

         /* Get the spob to modify. */
         hunk.dtype = HUNK_DATA_STRING;
         xmlr_attr_strd( cur, "name", hunk.u.name );

         /* Get the type. */
         if ( strcmp( buf, "add" ) == 0 )
            hunk.type = HUNK_TYPE_SPOB_ADD;
         else if ( strcmp( buf, "remove" ) == 0 )
            hunk.type = HUNK_TYPE_SPOB_REMOVE;
         else
            WARN( _( "Unidiff '%s': Unknown hunk type '%s' for spob '%s'." ),
                  diff->name, buf, hunk.u.name );

         /* Apply diff. */
         if ( diff_patchHunk( &hunk ) < 0 )
            diff_hunkFailed( diff, &hunk );
         else
            diff_hunkSuccess( diff, &hunk );
         continue;
      } else if ( xml_isNode( cur, "spob_virtual" ) ) {
         buf = xml_get( cur );
         if ( buf == NULL ) {
            WARN( _( "Unidiff '%s': Null hunk type." ), diff->name );
            continue;
         }

         hunk.target.type   = base.target.type;
         hunk.target.u.name = strdup( base.target.u.name );

         /* Get the spob to modify. */
         hunk.dtype = HUNK_DATA_STRING;
         xmlr_attr_strd( cur, "name", hunk.u.name );

         /* Get the type. */
         if ( strcmp( buf, "add" ) == 0 )
            hunk.type = HUNK_TYPE_VSPOB_ADD;
         else if ( strcmp( buf, "remove" ) == 0 )
            hunk.type = HUNK_TYPE_VSPOB_REMOVE;
         else
            WARN( _( "Unidiff '%s': Unknown hunk type '%s' for virtual spob "
                     "'%s'." ),
                  diff->name, buf, hunk.u.name );

         /* Apply diff. */
         if ( diff_patchHunk( &hunk ) < 0 )
            diff_hunkFailed( diff, &hunk );
         else
            diff_hunkSuccess( diff, &hunk );
         continue;
      } else if ( xml_isNode( cur, "jump" ) ) {
         buf = xml_get( cur );
         if ( buf == NULL ) {
            WARN( _( "Unidiff '%s': Null hunk type." ), diff->name );
            continue;
         }

         hunk.target.type   = base.target.type;
         hunk.target.u.name = strdup( base.target.u.name );

         /* Get the jump point to modify. */
         hunk.dtype = HUNK_DATA_STRING;
         xmlr_attr_strd( cur, "target", hunk.u.name );

         /* Get the type. */
         if ( strcmp( buf, "add" ) == 0 )
            hunk.type = HUNK_TYPE_JUMP_ADD;
         else if ( strcmp( buf, "remove" ) == 0 )
            hunk.type = HUNK_TYPE_JUMP_REMOVE;
         else
            WARN( _( "Unidiff '%s': Unknown hunk type '%s' for jump '%s'." ),
                  diff->name, buf, hunk.u.name );

         /* Apply diff. */
         if ( diff_patchHunk( &hunk ) < 0 )
            diff_hunkFailed( diff, &hunk );
         else
            diff_hunkSuccess( diff, &hunk );
         continue;
      }

      HUNK_STRD( "background", HUNK_TYPE_SSYS_BACKGROUND );
      HUNK_STRD( "features", HUNK_TYPE_SSYS_FEATURES );

      WARN( _( "Unidiff '%s' has unknown node '%s'." ), diff->name,
            node->name );
   } while ( xml_nextNode( cur ) );

   /* Clean up some stuff. */
   free( base.target.u.name );
   base.target.u.name = NULL;

   return 0;
}

/**
 * @brief Patches a tech.
 *
 *    @param diff Diff that is doing the patching.
 *    @param node Node containing the tech.
 *    @return 0 on success.
 */
static int diff_patchTech( UniDiff_t *diff, xmlNodePtr node )
{
   UniHunk_t  base, hunk;
   xmlNodePtr cur;

   /* Set the target. */
   memset( &base, 0, sizeof( UniHunk_t ) );
   base.target.type = HUNK_TARGET_TECH;
   xmlr_attr_strd( node, "name", base.target.u.name );
   if ( base.target.u.name == NULL ) {
      WARN( _( "Unidiff '%s' has an target node without a 'name' tag" ),
            diff->name );
      return -1;
   }

   /* Now parse the possible changes. */
   cur = node->xmlChildrenNode;
   do {
      xml_onlyNodes( cur );

      HUNK_STRD( "add", HUNK_TYPE_TECH_ADD );
      HUNK_STRD( "remove", HUNK_TYPE_TECH_REMOVE );

      WARN( _( "Unidiff '%s' has unknown node '%s'." ), diff->name,
            node->name );
   } while ( xml_nextNode( cur ) );

   /* Clean up some stuff. */
   free( base.target.u.name );
   base.target.u.name = NULL;

   return 0;
}

/**
 * @brief Patches a spob.
 *
 *    @param diff Diff that is doing the patching.
 *    @param node Node containing the spob.
 *    @return 0 on success.
 */
static int diff_patchSpob( UniDiff_t *diff, xmlNodePtr node )
{
   UniHunk_t  base, hunk;
   xmlNodePtr cur;

   /* Set the target. */
   memset( &base, 0, sizeof( UniHunk_t ) );
   base.target.type = HUNK_TARGET_SPOB;
   xmlr_attr_strd( node, "name", base.target.u.name );
   if ( base.target.u.name == NULL ) {
      WARN( _( "Unidiff '%s' has an target node without a 'name' tag" ),
            diff->name );
      return -1;
   }

   /* Now parse the possible changes. */
   cur = node->xmlChildrenNode;
   do {
      xml_onlyNodes( cur );

      HUNK_STRD( "faction", HUNK_TYPE_SPOB_FACTION );
      HUNK_UINT( "population", HUNK_TYPE_SPOB_POPULATION );
      HUNK_STRD( "displayname", HUNK_TYPE_SPOB_DISPLAYNAME );
      HUNK_STRD( "description", HUNK_TYPE_SPOB_DESCRIPTION );
      HUNK_STRD( "bar", HUNK_TYPE_SPOB_BAR );
      HUNK_STRD( "service_add", HUNK_TYPE_SPOB_SERVICE_ADD );
      HUNK_STRD( "service_remove", HUNK_TYPE_SPOB_SERVICE_REMOVE );
      HUNK_NONE( "nomissionspawn_add", HUNK_TYPE_SPOB_NOMISNSPAWN_ADD );
      HUNK_NONE( "nomissionspawn_remove", HUNK_TYPE_SPOB_NOMISNSPAWN_REMOVE );
      HUNK_STRD( "tech_add", HUNK_TYPE_SPOB_TECH_ADD );
      HUNK_STRD( "tech_remove", HUNK_TYPE_SPOB_TECH_REMOVE );
      HUNK_STRD( "tag_add", HUNK_TYPE_SPOB_TAG_ADD );
      HUNK_STRD( "tag_remove", HUNK_TYPE_SPOB_TAG_REMOVE );
      HUNK_CUST( "gfx_space", HUNK_TYPE_SPOB_SPACE, HUNK_DATA_STRING,
                 char str[PATH_MAX];
                 snprintf( str, sizeof( str ), SPOB_GFX_SPACE_PATH "%s",
                           xml_get( cur ) );
                 hunk.u.name = strdup( str ); );
      HUNK_CUST( "gfx_exterior", HUNK_TYPE_SPOB_EXTERIOR, HUNK_DATA_STRING,
                 char str[PATH_MAX];
                 snprintf( str, sizeof( str ), SPOB_GFX_EXTERIOR_PATH "%s",
                           xml_get( cur ) );
                 hunk.u.name = strdup( str ); );
      HUNK_STRD( "lua", HUNK_TYPE_SPOB_LUA );

      // cppcheck-suppress nullPointerRedundantCheck
      WARN( _( "Unidiff '%s' has unknown node '%s'." ), diff->name, cur->name );
   } while ( xml_nextNode( cur ) );

   /* Clean up some stuff. */
   free( base.target.u.name );
   base.target.u.name = NULL;

   return 0;
}

/**
 * @brief Patches a faction.
 *
 *    @param diff Diff that is doing the patching.
 *    @param node Node containing the spob.
 *    @return 0 on success.
 */
static int diff_patchFaction( UniDiff_t *diff, xmlNodePtr node )
{
   UniHunk_t  base, hunk;
   xmlNodePtr cur;
   char      *buf;

   /* Set the target. */
   memset( &base, 0, sizeof( UniHunk_t ) );
   base.target.type = HUNK_TARGET_FACTION;
   xmlr_attr_strd( node, "name", base.target.u.name );
   if ( base.target.u.name == NULL ) {
      WARN( _( "Unidiff '%s' has an target node without a 'name' tag" ),
            diff->name );
      return -1;
   }

   /* Now parse the possible changes. */
   cur = node->xmlChildrenNode;
   do {
      xml_onlyNodes( cur );

      HUNK_NONE( "visible", HUNK_TYPE_FACTION_VISIBLE );
      HUNK_NONE( "invisible", HUNK_TYPE_FACTION_INVISIBLE );

      if ( xml_isNode( cur, "faction" ) ) {
         buf = xml_get( cur );
         if ( buf == NULL ) {
            WARN( _( "Unidiff '%s': Null hunk type." ), diff->name );
            continue;
         }

         hunk.target.type   = base.target.type;
         hunk.target.u.name = strdup( base.target.u.name );

         /* Get the faction to set the association of. */
         xmlr_attr_strd( cur, "name", hunk.u.name );

         /* Get the type. */
         if ( strcmp( buf, "ally" ) == 0 )
            hunk.type = HUNK_TYPE_FACTION_ALLY;
         else if ( strcmp( buf, "enemy" ) == 0 )
            hunk.type = HUNK_TYPE_FACTION_ENEMY;
         else if ( strcmp( buf, "neutral" ) == 0 )
            hunk.type = HUNK_TYPE_FACTION_NEUTRAL;
         else
            WARN( _( "Unidiff '%s': Unknown hunk type '%s' for faction '%s'." ),
                  diff->name, buf, hunk.u.name );

         /* Apply diff. */
         if ( diff_patchHunk( &hunk ) < 0 )
            diff_hunkFailed( diff, &hunk );
         else
            diff_hunkSuccess( diff, &hunk );
         continue;
      }

      WARN( _( "Unidiff '%s' has unknown node '%s'." ), diff->name,
            node->name );
   } while ( xml_nextNode( cur ) );

   /* Clean up some stuff. */
   free( base.target.u.name );
   base.target.u.name = NULL;

   return 0;
}

/**
 * @brief Actually applies a diff in XML node form.
 *
 *    @param parent Node containing the diff information.
 *    @return 0 on success.
 */
static int diff_patch( xmlNodePtr parent )
{
   UniDiff_t *diff;
   xmlNodePtr node;
   int        nfailed;

   /* Prepare it. */
   diff = diff_newDiff();
   memset( diff, 0, sizeof( UniDiff_t ) );
   xmlr_attr_strd( parent, "name", diff->name );

   node = parent->xmlChildrenNode;
   do {
      xml_onlyNodes( node );
      if ( xml_isNode( node, "system" ) ) {
         diff_universe_changed = 1;
         diff_patchSystem( diff, node );
      } else if ( xml_isNode( node, "tech" ) )
         diff_patchTech( diff, node );
      else if ( xml_isNode( node, "spob" ) ) {
         diff_universe_changed = 1;
         diff_patchSpob( diff, node );
      } else if ( xml_isNode( node, "faction" ) ) {
         diff_universe_changed = 1;
         diff_patchFaction( diff, node );
      } else
         WARN( _( "Unidiff '%s' has unknown node '%s'." ), diff->name,
               node->name );
   } while ( xml_nextNode( node ) );

   nfailed = array_size( diff->failed );
   if ( nfailed > 0 ) {
      WARN( n_( "Unidiff '%s' failed to apply %d hunk.",
                "Unidiff '%s' failed to apply %d hunks.", nfailed ),
            diff->name, nfailed );
      for ( int i = 0; i < nfailed; i++ ) {
         UniHunk_t  *fail   = &diff->failed[i];
         char       *target = fail->target.u.name;
         const char *name;
         if ( ( fail->type < 0 ) || ( fail->type >= HUNK_TYPE_SENTINAL ) ||
              ( hunk_name[fail->type] == NULL ) ) {
            WARN( _( "Unknown unidiff hunk '%d'!" ), fail->type );
            name = N_( "unknown hunk" );
         } else
            name = hunk_name[fail->type];

         /* Have to handle all possible data cases. */
         switch ( fail->dtype ) {
         case HUNK_DATA_NONE:
            WARN( p_( "unidiff", "   [%s] %s" ), target, _( name ) );
            break;
         case HUNK_DATA_STRING:
            WARN( p_( "unidiff", "   [%s] %s: %s" ), target, _( name ),
                  fail->u.name );
            break;
         case HUNK_DATA_INT:
            WARN( p_( "unidiff", "   [%s] %s: %d" ), target, _( name ),
                  fail->u.data );
            break;
         case HUNK_DATA_FLOAT:
            WARN( p_( "unidiff", "   [%s] %s: %f" ), target, _( name ),
                  fail->u.fdata );
            break;

         default:
            WARN( p_( "unidiff", "   [%s] %s: UNKNOWN DATA" ), target,
                  _( name ) );
         }
      }
   }

   /* Update overlay map just in case. */
   ovr_refresh();
   return 0;
}

/**
 * @brief Applies a hunk and adds it to the diff.
 *
 *    @param hunk Hunk to apply.
 *    @return 0 on success.
 */
static int diff_patchHunk( UniHunk_t *hunk )
{
   Spob       *p;
   StarSystem *ssys;
   int         a, b;

   switch ( hunk->type ) {

   /* Adding an spob. */
   case HUNK_TYPE_SPOB_ADD:
      spob_luaInit( spob_get( hunk->u.name ) );
      diff_universe_changed = 1;
      return system_addSpob( system_get( hunk->target.u.name ), hunk->u.name );
   /* Removing an spob. */
   case HUNK_TYPE_SPOB_REMOVE:
      diff_universe_changed = 1;
      return system_rmSpob( system_get( hunk->target.u.name ), hunk->u.name );

   /* Adding an spob. */
   case HUNK_TYPE_VSPOB_ADD:
      diff_universe_changed = 1;
      return system_addVirtualSpob( system_get( hunk->target.u.name ),
                                    hunk->u.name );
   /* Removing an spob. */
   case HUNK_TYPE_VSPOB_REMOVE:
      diff_universe_changed = 1;
      return system_rmVirtualSpob( system_get( hunk->target.u.name ),
                                   hunk->u.name );

   /* Adding a Jump. */
   case HUNK_TYPE_JUMP_ADD:
      diff_universe_changed = 1;
      return system_addJump( system_get( hunk->target.u.name ), hunk->u.name );
   /* Removing a jump. */
   case HUNK_TYPE_JUMP_REMOVE:
      diff_universe_changed = 1;
      return system_rmJump( system_get( hunk->target.u.name ), hunk->u.name );

   /* Changing system background. */
   case HUNK_TYPE_SSYS_BACKGROUND:
      ssys             = system_get( hunk->target.u.name );
      hunk->o.name     = ssys->background;
      ssys->background = hunk->u.name;
      return 0;
   case HUNK_TYPE_SSYS_BACKGROUND_REVERT:
      ssys             = system_get( hunk->target.u.name );
      ssys->background = (char *)hunk->o.name;
      return 0;

   /* Changing system features designation. */
   case HUNK_TYPE_SSYS_FEATURES:
      ssys           = system_get( hunk->target.u.name );
      hunk->o.name   = ssys->features;
      ssys->features = hunk->u.name;
      return 0;
   case HUNK_TYPE_SSYS_FEATURES_REVERT:
      ssys           = system_get( hunk->target.u.name );
      ssys->features = (char *)hunk->o.name;
      return 0;

   /* Adding a tech. */
   case HUNK_TYPE_TECH_ADD:
      return tech_addItem( hunk->target.u.name, hunk->u.name );
   /* Removing a tech. */
   case HUNK_TYPE_TECH_REMOVE:
      return tech_rmItem( hunk->target.u.name, hunk->u.name );

   /* Changing spob faction. */
   case HUNK_TYPE_SPOB_FACTION:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      if ( p->presence.faction < 0 )
         hunk->o.name = NULL;
      else
         hunk->o.name = faction_name( p->presence.faction );
      diff_universe_changed = 1;
      return spob_setFaction( p, faction_get( hunk->u.name ) );
   case HUNK_TYPE_SPOB_FACTION_REMOVE:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      diff_universe_changed = 1;
      if ( hunk->o.name == NULL )
         return spob_setFaction( p, -1 );
      else
         return spob_setFaction( p, faction_get( hunk->o.name ) );

   /* Changing spob population. */
   case HUNK_TYPE_SPOB_POPULATION:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      hunk->o.data  = p->population;
      p->population = hunk->u.data;
      return 0;
   case HUNK_TYPE_SPOB_POPULATION_REMOVE:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      p->population = hunk->o.data;
      return 0;

   /* Changing spob displayname. */
   case HUNK_TYPE_SPOB_DISPLAYNAME:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      hunk->o.name = p->display;
      p->display   = hunk->u.name;
      return 0;
   case HUNK_TYPE_SPOB_DISPLAYNAME_REVERT:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      p->display = (char *)hunk->o.name;
      return 0;

   /* Changing spob description. */
   case HUNK_TYPE_SPOB_DESCRIPTION:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      hunk->o.name   = p->description;
      p->description = hunk->u.name;
      return 0;
   case HUNK_TYPE_SPOB_DESCRIPTION_REVERT:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      p->description = (char *)hunk->o.name;
      return 0;

   /* Changing spob bar description. */
   case HUNK_TYPE_SPOB_BAR:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      hunk->o.name       = p->bar_description;
      p->bar_description = hunk->u.name;
      return 0;
   case HUNK_TYPE_SPOB_BAR_REVERT:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      p->bar_description = (char *)hunk->o.name;
      return 0;

   /* Modifying spob services. */
   case HUNK_TYPE_SPOB_SERVICE_ADD:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      a = spob_getService( hunk->u.name );
      if ( a < 0 )
         return -1;
      if ( spob_hasService( p, a ) )
         return -1;
      spob_addService( p, a );
      diff_universe_changed = 1;
      return 0;
   case HUNK_TYPE_SPOB_SERVICE_REMOVE:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      a = spob_getService( hunk->u.name );
      if ( a < 0 )
         return -1;
      if ( !spob_hasService( p, a ) )
         return -1;
      spob_rmService( p, a );
      diff_universe_changed = 1;
      return 0;

   /* Modifying mission spawn. */
   case HUNK_TYPE_SPOB_NOMISNSPAWN_ADD:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      if ( spob_isFlag( p, SPOB_NOMISNSPAWN ) )
         return -1;
      spob_setFlag( p, SPOB_NOMISNSPAWN );
      return 0;
   case HUNK_TYPE_SPOB_NOMISNSPAWN_REMOVE:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      if ( !spob_isFlag( p, SPOB_NOMISNSPAWN ) )
         return -1;
      spob_rmFlag( p, SPOB_NOMISNSPAWN );
      return 0;

   /* Modifying tech stuff. */
   case HUNK_TYPE_SPOB_TECH_ADD:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      if ( p->tech == NULL )
         p->tech = tech_groupCreate();
      tech_addItemTech( p->tech, hunk->u.name );
      return 0;
   case HUNK_TYPE_SPOB_TECH_REMOVE:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      tech_rmItemTech( p->tech, hunk->u.name );
      return 0;

   /* Modifying tag stuff. */
   case HUNK_TYPE_SPOB_TAG_ADD:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      if ( p->tech == NULL )
         p->tech = tech_groupCreate();
      if ( p->tags == NULL )
         p->tags = array_create( char * );
      array_push_back( &p->tags, strdup( hunk->u.name ) );
      return 0;
   case HUNK_TYPE_SPOB_TAG_REMOVE:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      a = -1;
      for ( int i = 0; i < array_size( p->tags ); i++ ) {
         if ( strcmp( p->tags[i], hunk->u.name ) == 0 ) {
            a = i;
            break;
         }
      }
      if ( a < 0 )
         return -1; /* Didn't find tag! */
      free( p->tags[a] );
      array_erase( &p->tags, &p->tags[a], &p->tags[a + 1] );
      return 0;

   /* Changing spob space graphics. */
   case HUNK_TYPE_SPOB_SPACE:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      hunk->o.name          = p->gfx_spaceName;
      p->gfx_spaceName      = hunk->u.name;
      diff_universe_changed = 1;
      return 0;
   case HUNK_TYPE_SPOB_SPACE_REVERT:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      p->gfx_spaceName      = (char *)hunk->o.name;
      diff_universe_changed = 1;
      return 0;

   /* Changing spob exterior graphics. */
   case HUNK_TYPE_SPOB_EXTERIOR:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      hunk->o.name    = p->gfx_exterior;
      p->gfx_exterior = hunk->u.name;
      return 0;
   case HUNK_TYPE_SPOB_EXTERIOR_REVERT:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      p->gfx_exterior = (char *)hunk->o.name;
      return 0;

   /* Change Lua stuff. */
   case HUNK_TYPE_SPOB_LUA:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      hunk->o.name = p->lua_file;
      p->lua_file  = hunk->u.name;
      spob_luaInit( p );
      diff_universe_changed = 1;
      return 0;
   case HUNK_TYPE_SPOB_LUA_REVERT:
      p = spob_get( hunk->target.u.name );
      if ( p == NULL )
         return -1;
      p->lua_file = (char *)hunk->o.name;
      spob_luaInit( p );
      diff_universe_changed = 1;
      return 0;

   /* Making a faction visible. */
   case HUNK_TYPE_FACTION_VISIBLE:
      return faction_setInvisible( faction_get( hunk->target.u.name ), 0 );
   /* Making a faction invisible. */
   case HUNK_TYPE_FACTION_INVISIBLE:
      return faction_setInvisible( faction_get( hunk->target.u.name ), 1 );
   /* Making two factions allies. */
   case HUNK_TYPE_FACTION_ALLY:
      a = faction_get( hunk->target.u.name );
      b = faction_get( hunk->u.name );
      if ( areAllies( a, b ) )
         hunk->o.data = 'A';
      else if ( areEnemies( a, b ) )
         hunk->o.data = 'E';
      else
         hunk->o.data = 0;
      faction_addAlly( a, b );
      faction_addAlly( b, a );
      return 0;
   /* Making two factions enemies. */
   case HUNK_TYPE_FACTION_ENEMY:
      a = faction_get( hunk->target.u.name );
      b = faction_get( hunk->u.name );
      if ( areAllies( a, b ) )
         hunk->o.data = 'A';
      else if ( areEnemies( a, b ) )
         hunk->o.data = 'E';
      else
         hunk->o.data = 0;
      faction_addEnemy( a, b );
      faction_addEnemy( b, a );
      return 0;
   /* Making two factions neutral (removing enemy/ally statuses). */
   case HUNK_TYPE_FACTION_NEUTRAL:
      a = faction_get( hunk->target.u.name );
      b = faction_get( hunk->u.name );
      if ( areAllies( a, b ) )
         hunk->o.data = 'A';
      else if ( areEnemies( a, b ) )
         hunk->o.data = 'E';
      else
         hunk->o.data = 0;
      faction_rmAlly( a, b );
      faction_rmAlly( b, a );
      faction_rmEnemy( a, b );
      faction_rmEnemy( b, a );
      return 0;
   /* Resetting the alignment state of two factions. */
   case HUNK_TYPE_FACTION_REALIGN:
      a = faction_get( hunk->target.u.name );
      b = faction_get( hunk->u.name );
      if ( hunk->o.data == 'A' ) {
         faction_rmEnemy( a, b );
         faction_rmEnemy( b, a );
         faction_addAlly( a, b );
         faction_addAlly( b, a );
      } else if ( hunk->o.data == 'E' ) {
         faction_rmAlly( a, b );
         faction_rmAlly( b, a );
         faction_addEnemy( a, b );
         faction_addEnemy( b, a );
      } else {
         faction_rmAlly( a, b );
         faction_rmAlly( b, a );
         faction_rmEnemy( a, b );
         faction_rmEnemy( b, a );
      }
      return 0;

   default:
      WARN( _( "Unknown hunk type '%d'." ), hunk->type );
      break;
   }

   return -1;
}

/**
 * @brief Adds a hunk to the failed list.
 *
 *    @param diff Diff to add hunk to.
 *    @param hunk Hunk that failed to apply.
 */
static void diff_hunkFailed( UniDiff_t *diff, const UniHunk_t *hunk )
{
   if ( diff == NULL )
      return;
   if ( diff->failed == NULL )
      diff->failed = array_create( UniHunk_t );
   array_grow( &diff->failed ) = *hunk;
}

/**
 * @brief Adds a hunk to the applied list.
 *
 *    @param diff Diff to add hunk to.
 *    @param hunk Hunk that applied correctly.
 */
static void diff_hunkSuccess( UniDiff_t *diff, const UniHunk_t *hunk )
{
   if ( diff == NULL )
      return;
   if ( diff->applied == NULL )
      diff->applied = array_create( UniHunk_t );
   array_grow( &diff->applied ) = *hunk;
}

/**
 * @brief Removes a diff from the universe.
 *
 *    @param name Diff to remove.
 */
void diff_remove( const char *name )
{
   /* Check if already applied. */
   UniDiff_t *diff = diff_get( name );
   if ( diff == NULL )
      return;

   diff_removeDiff( diff );

   diff_checkUpdateUniverse();
}

/**
 * @brief Removes all active diffs. (Call before economy_destroy().)
 */
void diff_clear( void )
{
   while ( array_size( diff_stack ) > 0 )
      diff_removeDiff( &diff_stack[array_size( diff_stack ) - 1] );
   array_free( diff_stack );
   diff_stack = NULL;

   diff_checkUpdateUniverse();
}

/**
 * @brief Clean up after diff_loadAvailable().
 */
void diff_free( void )
{
   diff_clear();
   for ( int i = 0; i < array_size( diff_available ); i++ ) {
      UniDiffData_t *d = &diff_available[i];
      free( d->name );
      free( d->filename );
   }
   array_free( diff_available );
   diff_available = NULL;
}

/**
 * @brief Creates a new UniDiff_t for usage.
 *
 *    @return A newly created UniDiff_t.
 */
static UniDiff_t *diff_newDiff( void )
{
   /* Check if needs initialization. */
   if ( diff_stack == NULL )
      diff_stack = array_create( UniDiff_t );
   return &array_grow( &diff_stack );
}

/**
 * @brief Removes a diff.
 *
 *    @param diff Diff to remove.
 *    @return 0 on success.
 */
static int diff_removeDiff( UniDiff_t *diff )
{
   for ( int i = 0; i < array_size( diff->applied ); i++ ) {
      UniHunk_t hunk = diff->applied[i];
      /* Invert the type for reverting. */
      hunk.type = hunk_reverse[hunk.type];
      /* Patch. */
      if ( diff_patchHunk( &hunk ) )
         WARN( _( "Failed to remove hunk type '%d'." ), hunk.type );
   }

   diff_cleanup( diff );
   array_erase( &diff_stack, diff, &diff[1] );
   return 0;
}

/**
 * @brief Cleans up a diff.
 *
 *    @param diff Diff to clean up.
 */
static void diff_cleanup( UniDiff_t *diff )
{
   free( diff->name );
   for ( int i = 0; i < array_size( diff->applied ); i++ )
      diff_cleanupHunk( &diff->applied[i] );
   array_free( diff->applied );
   for ( int i = 0; i < array_size( diff->failed ); i++ )
      diff_cleanupHunk( &diff->failed[i] );
   array_free( diff->failed );
   memset( diff, 0, sizeof( UniDiff_t ) );
}

/**
 * @brief Cleans up a hunk.
 *
 *    @param hunk Hunk to clean up.
 */
void diff_cleanupHunk( UniHunk_t *hunk )
{
   free( hunk->target.u.name );
   hunk->target.u.name = NULL;

   if ( hunk->dtype == HUNK_DATA_STRING ) {
      free( hunk->u.name );
      hunk->u.name = NULL;
   }

   memset( hunk, 0, sizeof( UniHunk_t ) );
}

/**
 * @brief Saves the active diffs.
 *
 *    @param writer XML Writer to use.
 *    @return 0 on success.
 */
int diff_save( xmlTextWriterPtr writer )
{
   xmlw_startElem( writer, "diffs" );
   if ( diff_stack != NULL ) {
      for ( int i = 0; i < array_size( diff_stack ); i++ ) {
         const UniDiff_t *diff = &diff_stack[i];
         xmlw_elem( writer, "diff", "%s", diff->name );
      }
   }
   xmlw_endElem( writer ); /* "diffs" */
   return 0;
}

/**
 * @brief Loads the diffs.
 *
 *    @param parent Parent node containing diffs.
 *    @return 0 on success.
 */
int diff_load( xmlNodePtr parent )
{
   xmlNodePtr node;
   int        defer = diff_universe_defer;

   /* Don't update universe here. */
   diff_universe_defer   = 1;
   diff_universe_changed = 0;
   diff_nav_spob         = NULL;
   diff_nav_hyperspace   = NULL;
   diff_clear();
   diff_universe_defer = defer;

   node = parent->xmlChildrenNode;
   do {
      if ( xml_isNode( node, "diffs" ) ) {
         xmlNodePtr cur = node->xmlChildrenNode;
         do {
            if ( xml_isNode( cur, "diff" ) ) {
               const char *diffName = xml_get( cur );
               if ( diffName == NULL ) {
                  WARN( _( "Expected node \"diff\" to contain the name of a "
                           "unidiff. Was empty." ) );
                  continue;
               }
               diff_applyInternal( diffName, 0 );
               continue;
            }
         } while ( xml_nextNode( cur ) );
      }
   } while ( xml_nextNode( node ) );

   /* Update as necessary. */
   diff_checkUpdateUniverse();

   return 0;
}

/**
 * @brief Checks and updates the universe if necessary.
 */
static int diff_checkUpdateUniverse( void )
{
   Pilot *const *pilots;

   if ( !diff_universe_changed || diff_universe_defer )
      return 0;

   /* Update presences, then safelanes. */
   space_reconstructPresences();
   safelanes_recalculate();

   /* Re-compute the economy. */
   economy_execQueued();
   economy_initialiseCommodityPrices();

   /* Have to update planet graphics if necessary. */
   if ( cur_system != NULL ) {
      space_gfxUnload( cur_system );
      space_gfxLoad( cur_system );
   }

   /* Have to pilot targetting just in case. */
   pilots = pilot_getAll();
   for ( int i = 0; i < array_size( pilots ); i++ ) {
      Pilot *p          = pilots[i];
      p->nav_spob       = -1;
      p->nav_hyperspace = -1;

      /* Hack in case the pilot was actively jumping, this won't run the hook,
       * but I guess it's too much effort to properly fix for a situation that
       * will likely never happen. */
      if ( !pilot_isWithPlayer( p ) && pilot_isFlag( p, PILOT_HYPERSPACE ) )
         pilot_delete( p );
      else
         pilot_rmFlag(
            p, PILOT_HYPERSPACE ); /* Corner case player, just have it not crash
                                      and randomly stop the jump. */

      /* Have to reset in the case of starting. */
      if ( pilot_isFlag( p, PILOT_HYP_BEGIN ) ||
           pilot_isFlag( p, PILOT_HYP_BRAKE ) ||
           pilot_isFlag( p, PILOT_HYP_PREP ) )
         pilot_hyperspaceAbort( p );
   }

   /* Try to restore the targets if possible. */
   if ( diff_nav_spob != NULL ) {
      int found = 0;
      for ( int i = 0; i < array_size( cur_system->spobs ); i++ ) {
         if ( strcmp( cur_system->spobs[i]->name, diff_nav_spob ) == 0 ) {
            found              = 1;
            player.p->nav_spob = i;
            player_targetSpobSet( i );
            break;
         }
      }
      if ( !found )
         player_targetSpobSet( -1 );
   } else
      player_targetSpobSet( -1 );
   if ( diff_nav_hyperspace != NULL ) {
      int found = 0;
      for ( int i = 0; i < array_size( cur_system->jumps ); i++ ) {
         if ( strcmp( cur_system->jumps[i].target->name,
                      diff_nav_hyperspace ) == 0 ) {
            found                    = 1;
            player.p->nav_hyperspace = i;
            player_targetHyperspaceSet( i, 0 );
            break;
         }
      }
      if ( !found )
         player_targetHyperspaceSet( -1, 0 );
   } else
      player_targetHyperspaceSet( -1, 0 );

   diff_universe_changed = 0;
   return 1;
}

/**
 * @brief Sets whether or not to defer universe change stuff.
 *
 *    @param enable Whether or not to enable deferring.
 */
void unidiff_universeDefer( int enable )
{
   int defer           = diff_universe_defer;
   diff_universe_defer = enable;
   if ( defer && !enable )
      diff_checkUpdateUniverse();
}
