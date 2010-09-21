/*
 * =====================================================================================
 *
 *       Filename:  alert_history.c
 *
 *    Description:  Manages the history of alerts on a binary file
 *
 *        Version:  0.1
 *        Created:  18/09/2010 21:02:15
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  BlackLight (http://0x00.ath.cx), <blacklight@autistici.org>
 *        Licence:  GNU GPL v.3
 *        Company:  DO WHAT YOU WANT CAUSE A PIRATE IS FREE, YOU ARE A PIRATE!
 *
 * =====================================================================================
 */

#include	"spp_ai.h"

#include	<sys/stat.h>

typedef struct  {
	int gid;
	int sid;
	int rev;
} AI_alert_event_key;

typedef struct _AI_alert_event  {
	AI_alert_event_key      key;
	unsigned int            count;
	time_t                  timestamp;
	struct _AI_alert_event  *next;
	UT_hash_handle          hh;
} AI_alert_event;


PRIVATE AI_alert_event  *alerts_hash = NULL;


/**
 * FUNCTION: AI_alerts_hash_free
 * \brief  Free a hash table of alert events
 * \param  events  Hash table to be freed
 */

void
AI_alerts_hash_free ( AI_alert_event **events )
{
	AI_alert_event *hash_iterator = NULL,
				*list_iterator = NULL,
				*tmp           = NULL;

	while ( *events )
	{
		hash_iterator = *events;
		HASH_DEL ( *events, hash_iterator );
		list_iterator = hash_iterator;

		while ( list_iterator )
		{
			tmp = list_iterator->next;
			free ( list_iterator );
			list_iterator = tmp;
		}

		free ( hash_iterator );
	}

	*events = NULL;
}		/* -----  end of function AI_alerts_hash_free  ----- */

/**
 * \brief  Deserialize a alerts' hash table from the binary history file
 * \param  conf Configuration of the module
 * \return A void* pointer (to be casted to AI_alert_event*) to the stored hash table
 */

void*
AI_deserialize_alerts ( AI_config *conf )
{
	FILE                 *fp = NULL;
	struct stat          st;
	unsigned int         i, j,
			           lists_count = 0,
			           items_count = 0;
	AI_alert_event       *event_iterator = NULL,
				      *event_prev     = NULL,
				      *event_list     = NULL;
	AI_alert_event_key   key;

	if ( stat ( conf->alert_history_file, &st ) < 0 )
		return NULL;

	if ( ! S_ISREG ( st.st_mode ))
		_dpd.fatalMsg ( "AIPreproc: '%s' is not a regular file\n", conf->alert_history_file );

	if ( !( fp = fopen ( conf->alert_history_file, "r" )))
		_dpd.fatalMsg ( "AIPreproc: Unable to read from the file '%s'\n", conf->alert_history_file );

	AI_alerts_hash_free ( &alerts_hash );

	if ( fread ( &lists_count, sizeof ( unsigned int ), 1, fp ) <= 0 )
		_dpd.fatalMsg ( "AIPreproc: Malformed history file '%s'\n", conf->alert_history_file );

	/* Fill the hash table reading from the file */
	for ( i=0; i < lists_count; i++ )
	{
		event_iterator = NULL;
		event_prev     = NULL;

		if ( fread ( &items_count, sizeof ( unsigned int ), 1, fp ) <= 0 )
			_dpd.fatalMsg ( "AIPreproc: Malformed history file '%s'\n", conf->alert_history_file );
		
		for ( j=0; j < items_count; j++ )
		{
			if ( j == 0 )
			{
				if ( !( event_list = ( AI_alert_event* ) malloc ( sizeof ( AI_alert_event ))))
					_dpd.fatalMsg ( "AIPreproc: Fatal dynamic memory allocation error at %s:%d\n", __FILE__, __LINE__ );
				
				memset ( event_list, 0, sizeof ( AI_alert_event ));
				event_iterator = event_list;
			} else {
				if ( !( event_iterator = ( AI_alert_event* ) malloc ( sizeof ( AI_alert_event ))))
					_dpd.fatalMsg ( "AIPreproc: Fatal dynamic memory allocation error at %s:%d\n", __FILE__, __LINE__ );
				memset ( event_iterator, 0, sizeof ( AI_alert_event ));
			}

			event_iterator->count = items_count;

			if ( fread ( &( event_iterator->key ), sizeof ( event_iterator->key ), 1, fp ) <= 0 )
				_dpd.fatalMsg ( "AIPreproc: Malformed history file '%s'\n", conf->alert_history_file );

			if ( fread ( &( event_iterator->timestamp ), sizeof ( event_iterator->timestamp ), 1, fp ) <= 0 )
				_dpd.fatalMsg ( "AIPreproc: Malformed history file '%s'\n", conf->alert_history_file );

			if ( event_prev )
			{
				event_prev->next = event_iterator;
			}

			event_prev = event_iterator;
		}

		key = event_iterator->key;
		HASH_ADD ( hh, alerts_hash, key, sizeof ( key ), event_list );
	}

	fclose ( fp );
	return (void*) alerts_hash;
}		/* -----  end of function AI_deserialize_alerts  ----- */


/**
 * \brief  Serialize a buffer of alerts to the binary history file
 * \param  alerts_pool Buffer of alerts to be serialized
 * \param  alerts_pool_count Number of alerts in the buffer
 * \param  conf Configuration of the module
 */

void
AI_serialize_alerts ( AI_snort_alert **alerts_pool, unsigned int alerts_pool_count, AI_config *conf )
{
	unsigned int        i,
					hash_count      = 0,
					list_count      = 0;
	FILE                *fp             = NULL;
	AI_alert_event_key  key;
	AI_alert_event      *found          = NULL,
					*event          = NULL,
					*event_next     = NULL,
					*event_iterator = NULL;

	if ( !alerts_hash )
	{
		AI_deserialize_alerts ( conf );
	}

	for ( i=0; i < alerts_pool_count; i++ )
	{
		if ( !( event = ( AI_alert_event* ) malloc ( sizeof ( AI_alert_event ))))
			_dpd.fatalMsg ( "AIPreproc: Fatal dynamic memory allocation error at %s:%d\n", __FILE__, __LINE__ );

		memset ( event, 0, sizeof ( AI_alert_event ));
		key.gid = alerts_pool[i]->gid;
		key.sid = alerts_pool[i]->sid;
		key.rev = alerts_pool[i]->rev;
		event->key = key;
		event->timestamp = alerts_pool[i]->timestamp;

		HASH_FIND ( hh, alerts_hash, &key, sizeof ( key ), found );

		if ( !found )
		{
			event->count = 1;
			event->next  = NULL;
			HASH_ADD ( hh, alerts_hash, key, sizeof ( key ), event );
		} else {
			found->count++;
			event_next = NULL;

			for ( event_iterator = found; event_iterator->next; event_iterator = event_iterator->next )
			{
				/* Insert the new event in cronological order */
				if ( event_iterator->next->timestamp > event->timestamp )
				{
					event_next = event_iterator->next;
					break;
				}
			}

			if ( event_iterator )
				event_iterator->next = event;
			
			event->next = event_next;
		}
	}
	
	hash_count = HASH_COUNT ( alerts_hash );

	if ( !( fp = fopen ( conf->alert_history_file, "w" )))
		_dpd.fatalMsg ( "AIPreproc: Unable to write on '%s'\n", conf->alert_history_file );
	fwrite ( &hash_count, sizeof ( hash_count ), 1, fp );

	for ( event = alerts_hash; event; event = ( AI_alert_event* ) event->hh.next )
	{
		list_count = event->count;
		fwrite ( &list_count, sizeof ( list_count ), 1, fp );

		for ( event_iterator = event; event_iterator; event_iterator = event_iterator->next )
		{
			fwrite ( &(event_iterator->key), sizeof ( event_iterator->key ), 1, fp );
			fwrite ( &(event_iterator->timestamp), sizeof ( event_iterator->timestamp ), 1, fp );
		}
	}

	fclose ( fp );
}		/* -----  end of function AI_serialize_alerts  ----- */
