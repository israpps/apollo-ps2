#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <libmc.h>

#include "saves.h"
#include "common.h"
#include "sfo.h"
#include "settings.h"
#include "utils.h"

#define UTF8_CHAR_STAR		"\xE2\x98\x85"

#define CHAR_ICON_NET		"\x09"
#define CHAR_ICON_ZIP		"\x0C"
#define CHAR_ICON_COPY		"\x0B"
#define CHAR_ICON_SIGN		"\x06"
#define CHAR_ICON_USER		"\x07"
#define CHAR_ICON_LOCK		"\x08"
#define CHAR_ICON_WARN		"\x0F"


/*
 * Function:		endsWith()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Checks to see if a ends with b
 * Arguments:
 *	a:				String
 *	b:				Potential end
 * Return:			pointer if true, NULL if false
 */
static char* endsWith(const char * a, const char * b)
{
	int al = strlen(a), bl = strlen(b);
    
	if (al < bl)
		return NULL;

	a += (al - bl);
	while (*a)
		if (toupper(*a++) != toupper(*b++)) return NULL;

	return (char*) (a - bl);
}

/*
 * Function:		readFile()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		reads the contents of a file into a new buffer
 * Arguments:
 *	path:			Path to file
 * Return:			Pointer to the newly allocated buffer
 */
char * readTextFile(const char * path, long* size)
{
	FILE *f = fopen(path, "rb");

	if (!f)
		return NULL;

	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (fsize <= 0)
	{
		fclose(f);
		return NULL;
	}

	char * string = malloc(fsize + 1);
	fread(string, fsize, 1, f);
	fclose(f);

	string[fsize] = 0;
	if (size)
		*size = fsize;

	return string;
}

static code_entry_t* _createCmdCode(uint8_t type, const char* name, char code)
{
	code_entry_t* entry = (code_entry_t *)calloc(1, sizeof(code_entry_t));
	entry->type = type;
	entry->name = strdup(name);
	asprintf(&entry->codes, "%c", code);

	return entry;
}

static option_entry_t* _initOptions(int count)
{
	option_entry_t* options = (option_entry_t*)calloc(1, sizeof(option_entry_t));

	options->sel = -1;
	options->size = count;
	options->value = calloc (count, sizeof(char *));
	options->name = calloc (count, sizeof(char *));

	return options;
}

static option_entry_t* _createMcOptions(int count, const char* name, char value)
{
	option_entry_t* options = _initOptions(count);

	asprintf(&options->name[0], "%s (%s)", name, MC0_PATH);
	asprintf(&options->value[0], "%c%c", value, STORAGE_MC0);
	asprintf(&options->name[1], "%s (%s)", name, MC1_PATH);
	asprintf(&options->value[1], "%c%c", value, STORAGE_MC1);

	return options;
}

static option_entry_t* _createExtOptions(int count, const char* name, char value)
{
	option_entry_t* options = _initOptions(count);

	asprintf(&options->name[0], "%s (%s)", name, USB_PATH);
	asprintf(&options->value[0], "%c%c", value, STORAGE_MASS);
	asprintf(&options->name[1], "%s (%s)", name, "host:/");
	asprintf(&options->value[1], "%c%c", value, STORAGE_HOST);

	return options;
}

static save_entry_t* _createSaveEntry(uint16_t flag, const char* name)
{
	save_entry_t* entry = (save_entry_t *)calloc(1, sizeof(save_entry_t));
	entry->flags = flag;
	entry->name = strdup(name);

	return entry;
}

static void _walk_dir_list(const char* startdir, const char* inputdir, const char* mask, list_t* list)
{
	char fullname[256];	
	struct dirent *dirp;
	int len = strlen(startdir);
	DIR *dp = opendir(inputdir);

	if (!dp) {
		LOG("Failed to open input directory: '%s'", inputdir);
		return;
	}

	while ((dirp = readdir(dp)) != NULL)
	{
		if ((strcmp(dirp->d_name, ".")  == 0) || (strcmp(dirp->d_name, "..") == 0) ||
			(strcmp(dirp->d_name, "ICON0.PNG") == 0) || (strcmp(dirp->d_name, "PARAM.SFO") == 0) || (strcmp(dirp->d_name,"PIC1.PNG") == 0) ||
			(strcmp(dirp->d_name, "icon.sys") == 0) || (strcmp(dirp->d_name, "SND0.AT3") == 0))
			continue;

		snprintf(fullname, sizeof(fullname), "%s%s", inputdir, dirp->d_name);

		if (wildcard_match_icase(dirp->d_name, mask))
		{
			//LOG("Adding file '%s'", fullname+len);
			list_append(list, strdup(fullname+len));
		}
	}
	closedir(dp);
}

static option_entry_t* _getFileOptions(const char* save_path, const char* mask, uint8_t is_cmd)
{
	char *filename;
	list_t* file_list;
	list_node_t* node;
	int i = 0;
	option_entry_t* opt;

	LOG("Loading filenames {%s} from '%s'...", mask, save_path);

	file_list = list_alloc();
	_walk_dir_list(save_path, save_path, mask, file_list);

	if (!list_count(file_list))
	{
		is_cmd = 0;
		asprintf(&filename, CHAR_ICON_WARN " --- %s%s --- " CHAR_ICON_WARN, save_path, mask);
		list_append(file_list, filename);
	}

	opt = _initOptions(list_count(file_list));

	for (node = list_head(file_list); (filename = list_get(node)); node = list_next(node))
	{
		LOG("Adding '%s' (%s)", filename, mask);
		opt->name[i] = filename;

		if (is_cmd)
			asprintf(&opt->value[i], "%c", is_cmd);
		else
			asprintf(&opt->value[i], "%s", mask);

		i++;
	}

	list_free(file_list);

	return opt;
}

static void _addBackupCommands(save_entry_t* item)
{
	code_entry_t* cmd;

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_SIGN " Apply Changes", CMD_RESIGN_SAVE);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_USER " View Save Details", CMD_VIEW_DETAILS);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_NULL, "----- " UTF8_CHAR_STAR " File Backup " UTF8_CHAR_STAR " -----", CMD_CODE_NULL);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Copy save game", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = (item->flags & SAVE_FLAG_MEMCARD) ?
		_createExtOptions(2, "Copy Save to Mass Storage", CMD_COPY_SAVE_USB) :
		_createMcOptions(2, "Copy Save to Memory Card", CMD_COPY_SAVE_HDD);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_ZIP " Export save game to Zip", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createExtOptions(2, "Export Zip to Mass Storage", CMD_EXPORT_ZIP_USB);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Export single save files", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _getFileOptions(item->path, "*", CMD_EXPORT_DATA_FILE);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Import single save files", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _getFileOptions(item->path, "*", CMD_IMPORT_DATA_FILE);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_SIGN " Hex Edit save game files", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _getFileOptions(item->path, "*", CMD_HEX_EDIT_FILE);
	list_append(item->codes, cmd);
}

static option_entry_t* _getSaveTitleIDs(const char* title_id)
{
	int count = 1;
	option_entry_t* opt;
	char tmp[16];
	const char *ptr;
	const char *tid = NULL;//get_game_title_ids(title_id);

	if (!tid)
		tid = title_id;

	ptr = tid;
	while (*ptr)
		if (*ptr++ == '/') count++;

	LOG("Adding (%d) TitleIDs=%s", count, tid);

	opt = _initOptions(count);
	int i = 0;

	ptr = tid;
	while (*ptr++)
	{
		if ((*ptr == '/') || (*ptr == 0))
		{
			memset(tmp, 0, sizeof(tmp));
			strncpy(tmp, tid, ptr - tid);
			asprintf(&opt->name[i], "%s", tmp);
			asprintf(&opt->value[i], "%c", SFO_CHANGE_TITLE_ID);
			tid = ptr+1;
			i++;
		}
	}

	return opt;
}

static void add_ps1_commands(save_entry_t* save)
{
	char path[256];
	code_entry_t* cmd;

	cmd = _createCmdCode(PATCH_NULL, "----- " UTF8_CHAR_STAR " VMP Memory Cards " UTF8_CHAR_STAR " -----", CMD_CODE_NULL);
	list_append(save->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_SIGN " Resign Memory Card", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _getFileOptions(save->path, "*.VMP", CMD_RESIGN_VMP);
	list_append(save->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Export Memory Card to .MCR", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _getFileOptions(save->path, "*.VMP", CMD_EXP_VMP2MCR);
	list_append(save->codes, cmd);

	snprintf(path, sizeof(path), PS1_SAVES_PATH_HDD "%s/", save->title_id);
	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Import .MCR files to SCEVMC0.VMP", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _getFileOptions(path, "*.MCR", CMD_IMP_MCR2VMP0);
	list_append(save->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Import .MCR files to SCEVMC1.VMP", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _getFileOptions(path, "*.MCR", CMD_IMP_MCR2VMP1);
	list_append(save->codes, cmd);

	return;
}

static void add_ps2_commands(save_entry_t* item)
{
	code_entry_t* cmd;

	cmd = _createCmdCode(PATCH_NULL, "----- " UTF8_CHAR_STAR " Save Game Exports " UTF8_CHAR_STAR " -----", CMD_CODE_NULL);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Export save to .PSU format", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createExtOptions(2, "Export .PSU to Storage", CMD_EXP_PS2SAVE);
	cmd->options[0].id = FILE_TYPE_PSU;
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Export save to .PSV format", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createExtOptions(2, "Export .PSV to Storage", CMD_EXP_PS2SAVE);
	cmd->options[0].id = FILE_TYPE_PSV;
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Export save to .CBS format", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createExtOptions(2, "Export .CBS to Storage", CMD_EXP_PS2SAVE);
	cmd->options[0].id = FILE_TYPE_CBS;
	list_append(item->codes, cmd);

	return;
}

option_entry_t* get_file_entries(const char* path, const char* mask)
{
	return _getFileOptions(path, mask, CMD_CODE_NULL);
}

/*
 * Function:		ReadLocalCodes()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Reads an entire NCL file into an array of code_entry
 * Arguments:
 *	path:			Path to ncl
 *	_count_count:	Pointer to int (set to the number of codes within the ncl)
 * Return:			Returns an array of code_entry, null if failed to load
 */
int ReadCodes(save_entry_t * save)
{
	code_entry_t * code;
	char filePath[256];
	char * buffer = NULL;

	save->codes = list_alloc();
	_addBackupCommands(save);

	if (save->flags & SAVE_FLAG_PS1)
		add_ps1_commands(save);
	else
		add_ps2_commands(save);

	snprintf(filePath, sizeof(filePath), APOLLO_DATA_PATH "%s.savepatch", save->title_id);
	if ((buffer = readTextFile(filePath, NULL)) == NULL)
		goto skip_end;

	code = _createCmdCode(PATCH_NULL, "----- " UTF8_CHAR_STAR " Cheats " UTF8_CHAR_STAR " -----", CMD_CODE_NULL);	
	list_append(save->codes, code);

	code = _createCmdCode(PATCH_COMMAND, CHAR_ICON_USER " View Raw Patch File", CMD_VIEW_RAW_PATCH);
	list_append(save->codes, code);

	LOG("Loading BSD codes '%s'...", filePath);
	load_patch_code_list(buffer, save->codes, &get_file_entries, save->path);
	free (buffer);

skip_end:
	LOG("Loaded %ld codes", list_count(save->codes));

	return list_count(save->codes);
}

int ReadTrophies(save_entry_t * game)
{
	return (0);
}

/*
 * Function:		ReadOnlineSaves()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Downloads an entire NCL file into an array of code_entry
 * Arguments:
 *	filename:		File name ncl
 *	_count_count:	Pointer to int (set to the number of codes within the ncl)
 * Return:			Returns an array of code_entry, null if failed to load
 */
int ReadOnlineSaves(save_entry_t * game)
{
	code_entry_t* item;
	char path[256];
	snprintf(path, sizeof(path), APOLLO_LOCAL_CACHE "%s.txt", game->title_id);

	if (file_exists(path) == SUCCESS && strcmp(apollo_config.save_db, ONLINE_URL) == 0)
	{
		struct stat stats;
		stat(path, &stats);
		// re-download if file is +1 day old
		if ((stats.st_mtime + ONLINE_CACHE_TIMEOUT) < time(NULL))
			http_download(game->path, "saves.txt", path, 0);
	}
	else
	{
		if (!http_download(game->path, "saves.txt", path, 0))
			return -1;
	}

	long fsize;
	char *data = readTextFile(path, &fsize);
	
	char *ptr = data;
	char *end = data + fsize;

	game->codes = list_alloc();

	while (ptr < end && *ptr)
	{
		const char* content = ptr;

		while (ptr < end && *ptr != '\n' && *ptr != '\r')
		{
			ptr++;
		}
		*ptr++ = 0;

		if (content[12] == '=')
		{
			snprintf(path, sizeof(path), CHAR_ICON_ZIP " %s", content + 13);
			item = _createCmdCode(PATCH_COMMAND, path, CMD_CODE_NULL);
			asprintf(&item->file, "%.12s", content);

			item->options_count = 1;
			item->options = _createMcOptions(3, "Download to Memory Card", CMD_DOWNLOAD_USB);
			asprintf(&item->options->name[2], "Download to Mass Storage (mass:/)");
			asprintf(&item->options->value[2], "%c%c", CMD_DOWNLOAD_USB, STORAGE_MASS);
			list_append(game->codes, item);

			LOG("[%s%s] %s", game->path, item->file, item->name + 1);
		}

		if (ptr < end && *ptr == '\r')
		{
			ptr++;
		}
		if (ptr < end && *ptr == '\n')
		{
			ptr++;
		}
	}

	if (data) free(data);
	LOG("Loaded %d saves", list_count(game->codes));

	return (list_count(game->codes));
}

list_t * ReadBackupList(const char* userPath)
{
	save_entry_t * item;
	code_entry_t * cmd;
	list_t *list = list_alloc();

	item = _createSaveEntry(SAVE_FLAG_ZIP, CHAR_ICON_ZIP " Extract Archives (Zip, 7z)");
	item->path = strdup(menu_options[3].options[apollo_config.storage]);
	item->type = FILE_TYPE_ZIP;
	list_append(list, item);
/*
	item = _createSaveEntry(0, CHAR_ICON_NET " Network Tools");
	item->path = strdup(MC0_PATH);
	item->type = FILE_TYPE_NET;
	list_append(list, item);
*/
	return list;
}

int ReadBackupCodes(save_entry_t * bup)
{
	code_entry_t * cmd;
	char tmp[256];

	switch(bup->type)
	{
	case FILE_TYPE_ZIP:
		break;

	case FILE_TYPE_NET:
		bup->codes = list_alloc();
//		cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_NET " URL link Downloader (http, https, ftp, ftps)", CMD_URL_DOWNLOAD);
//		list_append(bup->codes, cmd);
		cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_NET " Local Web Server (full system access)", CMD_NET_WEBSERVER);
		list_append(bup->codes, cmd);
		return list_count(bup->codes);
/*
	case FILE_TYPE_PRX:
		bup->codes = list_alloc();

		cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_USER " Install Save-game Key Dumper plugin", CMD_SETUP_PLUGIN);
		cmd->codes[1] = 1;
		list_append(bup->codes, cmd);
		cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_LOCK " Disable Save-game Key Dumper plugin", CMD_SETUP_PLUGIN);
		cmd->codes[1] = 0;
		list_append(bup->codes, cmd);

		return list_count(bup->codes);
*/
	default:
		return 0;
	}

	bup->codes = list_alloc();

	LOG("Loading files from '%s'...", bup->path);

	DIR *d;
	struct dirent *dir;
	d = opendir(bup->path);

	if (d)
	{
		while ((dir = readdir(d)) != NULL)
		{
			if (!endsWith(dir->d_name, ".RAR") && !endsWith(dir->d_name, ".ZIP") && !endsWith(dir->d_name, ".7Z"))
				continue;

			snprintf(tmp, sizeof(tmp), CHAR_ICON_ZIP " Extract %s%s", bup->path, dir->d_name);
			cmd = _createCmdCode(PATCH_COMMAND, tmp, CMD_EXTRACT_ARCHIVE);
			asprintf(&cmd->file, "%s%s", bup->path, dir->d_name);

			LOG("[%s] name '%s'", cmd->file, cmd->name +2);
			list_append(bup->codes, cmd);
		}
		closedir(d);
	}

	if (!list_count(bup->codes))
	{
		list_free(bup->codes);
		bup->codes = NULL;
		return 0;
	}

	LOG("%ld items loaded", list_count(bup->codes));

	return list_count(bup->codes);
}

/*
 * Function:		UnloadGameList()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Free entire array of game_entry
 * Arguments:
 *	list:			Array of game_entry to free
 *	count:			number of game entries
 * Return:			void
 */
void UnloadGameList(list_t * list)
{
	list_node_t *node, *nc;
	save_entry_t *item;
	code_entry_t *code;

	for (node = list_head(list); (item = list_get(node)); node = list_next(node))
	{
		if (item->name)
		{
			free(item->name);
			item->name = NULL;
		}

		if (item->path)
		{
			free(item->path);
			item->path = NULL;
		}

		if (item->dir_name)
		{
			free(item->dir_name);
			item->dir_name = NULL;
		}

		if (item->title_id)
		{
			free(item->title_id);
			item->title_id = NULL;
		}
		
		if (item->codes)
		{
			for (nc = list_head(item->codes); (code = list_get(nc)); nc = list_next(nc))
			{
				if (code->codes)
				{
					free (code->codes);
					code->codes = NULL;
				}
				if (code->name)
				{
					free (code->name);
					code->name = NULL;
				}
				if (code->options && code->options_count > 0)
				{
					for (int z = 0; z < code->options_count; z++)
					{
						for (int j = 0; j < code->options[z].size; j++)
						{
							free(code->options[z].name[j]);
							free(code->options[z].value[j]);
						}
						if (code->options[z].line)
							free(code->options[z].line);
						if (code->options[z].name)
							free(code->options[z].name);
						if (code->options[z].value)
							free(code->options[z].value);
					}
					
					free (code->options);
				}
			}
			
			list_free(item->codes);
			item->codes = NULL;
		}
	}

	list_free(list);
	
	LOG("UnloadGameList() :: Successfully unloaded game list");
}

int sortCodeList_Compare(const void* a, const void* b)
{
	return strcasecmp(((code_entry_t*) a)->name, ((code_entry_t*) b)->name);
}

/*
 * Function:		qsortSaveList_Compare()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Compares two game_entry for QuickSort
 * Arguments:
 *	a:				First code
 *	b:				Second code
 * Return:			1 if greater, -1 if less, or 0 if equal
 */
int sortSaveList_Compare(const void* a, const void* b)
{
	return strcasecmp(((save_entry_t*) a)->name, ((save_entry_t*) b)->name);
}

int sortSaveList_Compare_TitleID(const void* a, const void* b)
{
	char* ta = ((save_entry_t*) a)->title_id;
	char* tb = ((save_entry_t*) b)->title_id;

	if (!ta)
		return (-1);

	if (!tb)
		return (1);

	return strcasecmp(ta, tb);
}

static void read_usb_encrypted_saves(const char* userPath, list_t *list, uint64_t account)
{
	DIR *d, *d2;
	struct dirent *dir, *dir2;
	save_entry_t *item;
	char savePath[256];

	d = opendir(userPath);

	if (!d)
		return;

	while ((dir = readdir(d)) != NULL)
	{
		if (S_ISDIR(dir->d_stat.st_mode) || strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
			continue;

		snprintf(savePath, sizeof(savePath), "%s%s", userPath, dir->d_name);
		d2 = opendir(savePath);

		if (!d2)
			continue;

		LOG("Reading %s...", savePath);

		while ((dir2 = readdir(d2)) != NULL)
		{
			if (!S_ISDIR(dir2->d_stat.st_mode) || endsWith(dir2->d_name, ".bin"))
				continue;

			snprintf(savePath, sizeof(savePath), "%s%s/%s.bin", userPath, dir->d_name, dir2->d_name);
			if (file_exists(savePath) != SUCCESS)
				continue;

			snprintf(savePath, sizeof(savePath), "(Encrypted) %s/%s", dir->d_name, dir2->d_name);
			item = _createSaveEntry(SAVE_FLAG_PSP | 0, savePath);
			item->type = FILE_TYPE_PSV;

			asprintf(&item->path, "%s%s/", userPath, dir->d_name);
			asprintf(&item->title_id, "%.9s", dir->d_name);
			item->dir_name = strdup(dir2->d_name);

//			if (apollo_config.account_id == account)
//				item->flags |= SAVE_FLAG_OWNER;

			snprintf(savePath, sizeof(savePath), "%s%s/%s", userPath, dir->d_name, dir2->d_name);
			
			uint64_t size = 0;
			get_file_size(savePath, &size);
//			item->blocks = size / ORBIS_SAVE_DATA_BLOCK_SIZE;

			LOG("[%s] F(%X) name '%s'", item->title_id, item->flags, item->name);
			list_append(list, item);

		}
		closedir(d2);
	}

	closedir(d);
}

static void read_ps2_savegames(const char* userPath, list_t *list, int flags)
{
	DIR *d;
	mcIcon iconsys;
	struct dirent *dir;
	save_entry_t *item;
	char sysPath[256];

	d = opendir(userPath);
	if (!d)
		return;

	while ((dir = readdir(d)) != NULL)
	{
		if (!S_ISDIR(dir->d_stat.st_mode) || strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
			continue;

		snprintf(sysPath, sizeof(sysPath), "%s%s/icon.sys", userPath, dir->d_name);

		LOG("Reading %s...", sysPath);
		if (read_file(sysPath, (uint8_t*) &iconsys, sizeof(mcIcon)) < 0) {
			LOG("Unable to read from '%s'", sysPath);
			continue;
		}

		if (iconsys.nlOffset)
		{
			iconsys.nlOffset /= 2;
			memmove(&iconsys.title[iconsys.nlOffset+1], &iconsys.title[iconsys.nlOffset], sizeof(iconsys.title) - iconsys.nlOffset*2);
			iconsys.title[iconsys.nlOffset] = 0x5081;
		}
		sjis2ascii((uint8_t*) iconsys.title);
		item = _createSaveEntry(SAVE_FLAG_PS2 | flags, (char*) iconsys.title);
		item->type = FILE_TYPE_PS2;
		item->icon = strdup(iconsys.copy);
		item->dir_name = strdup(dir->d_name);
		asprintf(&item->title_id, "%.10s", dir->d_name+2);
		asprintf(&item->path, "%s%s/", userPath, dir->d_name);

		if(item->title_id[4] == '-')
			memmove(&item->title_id[4], &item->title_id[5], 6);

		LOG("[%s] F(%X) name '%s'", item->title_id, item->flags, item->name);
		list_append(list, item);
	}

	closedir(d);
}

static void read_usb_savegames(const char* userPath, list_t *list)
{
	char path[256];

	snprintf(path, sizeof(path), "%sPS2/SAVEDATA/", userPath);
	read_ps2_savegames(path, list, 0);
}

static int set_psx_import_codes(save_entry_t* item)
{
	code_entry_t* cmd;
	item->codes = list_alloc();

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_USER " View Save Details", CMD_VIEW_DETAILS);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Import to Memory Card", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createMcOptions(2, "Import to MemCard", CMD_IMP_SAVE_MC);
	list_append(item->codes, cmd);

	return list_count(item->codes);
}

static void read_psx_savegames(const char* userPath, const char* folder, list_t *list)
{
	DIR *d;
	struct dirent *dir;
	save_entry_t *item;
	char psvPath[256];
	char data[64];
	int type, toff;

	snprintf(psvPath, sizeof(psvPath), "%s%s", userPath, folder);
	d = opendir(psvPath);

	if (!d)
		return;

	while ((dir = readdir(d)) != NULL)
	{
		if (!S_ISREG(dir->d_stat.st_mode))
			continue;

		if (endsWith(dir->d_name, ".PSX"))
		{
			toff = 0;
			type = FILE_TYPE_PSX;
		}
		else if (endsWith(dir->d_name, ".MCS"))
		{
			toff = 0x0A;
			type = FILE_TYPE_MCS;
		}
		else if (endsWith(dir->d_name, ".MAX"))
		{
			toff = 0x10;
			type = FILE_TYPE_MAX;
		}
		else if (endsWith(dir->d_name, ".CBS"))
		{
			toff = 0x14;
			type = FILE_TYPE_CBS;
		}
		else if (endsWith(dir->d_name, ".PSU"))
		{
			toff = 0x40;
			type = FILE_TYPE_PSU;
		}
		else if (endsWith(dir->d_name, ".PSV"))
		{
			toff = 0x80;
			type = FILE_TYPE_PSV;
		}
		else if (endsWith(dir->d_name, ".XPS") || endsWith(dir->d_name, ".SPS"))
		{
			toff = 0x04;
			type = FILE_TYPE_XPS;
		}
		else
			continue;

		snprintf(psvPath, sizeof(psvPath), "%s%s%s", userPath, folder, dir->d_name);
		LOG("Reading %s...", psvPath);

		FILE *fp = fopen(psvPath, "rb");
		if (!fp) {
			LOG("Unable to open '%s'", psvPath);
			continue;
		}

		fseek(fp, toff, SEEK_SET);
		fread(data, 1, sizeof(data), fp);
		fclose(fp);

		item = _createSaveEntry(SAVE_FLAG_PACKED, dir->d_name);
		set_psx_import_codes(item);

		item->flags |= (type == FILE_TYPE_PSX || type == FILE_TYPE_MCS) ? SAVE_FLAG_PS1 : SAVE_FLAG_PS2;
		item->type = type;
		item->path = strdup(psvPath);
		asprintf(&item->title_id, "%.10s", data+2);

		if(item->title_id[4] == '-')
			memmove(&item->title_id[4], &item->title_id[5], 6);

		LOG("[%s] F(%X) name '%s'", item->title_id, item->flags, item->name);
		list_append(list, item);
	}

	closedir(d);
}

/*
 * Function:		ReadUserList()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Reads the entire userlist folder into a game_entry array
 * Arguments:
 *	gmc:			Set as the number of games read
 * Return:			Pointer to array of game_entry, null if failed
 */
list_t * ReadUsbList(const char* userPath)
{
	save_entry_t *item;
	code_entry_t *cmd;
	list_t *list;

	if (dir_exists(userPath) != SUCCESS)
		return NULL;

	list = list_alloc();

	item = _createSaveEntry(SAVE_FLAG_PS2, CHAR_ICON_COPY " Bulk Save Management");
	item->type = FILE_TYPE_MENU;
	item->codes = list_alloc();
	item->path = strdup(userPath);
	//bulk management hack
	item->dir_name = malloc(sizeof(void**));
	((void**)item->dir_name)[0] = list;

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Copy selected Saves to Memory Card", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createMcOptions(2, "Copy Saves to MemCard", CMD_COPY_SAVES_HDD);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Copy all Saves to Memory Card", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createMcOptions(2, "Copy Saves to MemCard", CMD_COPY_ALL_SAVES_HDD);
	list_append(item->codes, cmd);

//	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_NET " Start local Web Server", CMD_SAVE_WEBSERVER);
//	list_append(item->codes, cmd);
	list_append(list, item);

	read_usb_savegames(userPath, list);
	read_psx_savegames(userPath, "PS2/SAVEDATA/", list);
//	read_psx_savegames(userPath, "PS1/SAVEDATA/", list);

	return list;
}

list_t * ReadUserList(const char* userPath)
{
	save_entry_t *item;
	code_entry_t *cmd;
	list_t *list;

	if (dir_exists(userPath) != SUCCESS)
		return NULL;

	list = list_alloc();

	item = _createSaveEntry(SAVE_FLAG_PS2, CHAR_ICON_COPY " Bulk Save Management");
	item->type = FILE_TYPE_MENU;
	item->codes = list_alloc();
	item->path = strdup(userPath);
	//bulk management hack
	item->dir_name = malloc(sizeof(void**));
	((void**)item->dir_name)[0] = list;

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Copy selected Saves to Backup Storage", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createExtOptions(2, "Copy Saves to Backup Storage", CMD_COPY_SAVES_USB);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Copy all Saves to Backup Storage", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createExtOptions(2, "Copy Saves to Backup Storage", CMD_COPY_ALL_SAVES_USB);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " .PSU Export selected Saves to Backup Storage", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createExtOptions(2, "Copy Saves to Backup Storage", CMD_EXPORT_SAVES);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " .PSU Export All Saves to Backup Storage", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createExtOptions(2, "Copy Saves to Backup Storage", CMD_EXPORT_ALL_SAVES);
	list_append(item->codes, cmd);

//	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_NET " Start local Web Server", CMD_SAVE_WEBSERVER);
//	list_append(item->codes, cmd);
	list_append(list, item);

	read_ps2_savegames(userPath, list, SAVE_FLAG_MEMCARD);

	return list;
}

/*
 * Function:		ReadOnlineList()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Downloads the entire gamelist file into a game_entry array
 * Arguments:
 *	gmc:			Set as the number of games read
 * Return:			Pointer to array of game_entry, null if failed
 */
static void _ReadOnlineListEx(const char* urlPath, uint16_t flag, list_t *list)
{
	save_entry_t *item;
	char path[256];

	snprintf(path, sizeof(path), APOLLO_LOCAL_CACHE "%04X_games.txt", flag);

	if (file_exists(path) == SUCCESS && strcmp(apollo_config.save_db, ONLINE_URL) == 0)
	{
		struct stat stats;
		stat(path, &stats);
		// re-download if file is +1 day old
		if ((stats.st_mtime + ONLINE_CACHE_TIMEOUT) < time(NULL))
			http_download(urlPath, "games.txt", path, 0);
	}
	else
	{
		if (!http_download(urlPath, "games.txt", path, 0))
			return;
	}
	
	long fsize;
	char *data = readTextFile(path, &fsize);
	
	char *ptr = data;
	char *end = data + fsize;

	while (ptr < end && *ptr)
	{
		char *tmp, *content = ptr;

		while (ptr < end && *ptr != '\n' && *ptr != '\r')
		{
			ptr++;
		}
		*ptr++ = 0;

		if ((tmp = strchr(content, '=')) != NULL)
		{
			*tmp++ = 0;
			item = _createSaveEntry(flag | SAVE_FLAG_ONLINE, tmp);
			item->title_id = strdup(content);
			asprintf(&item->path, "%s%s/", urlPath, item->title_id);

			LOG("+ [%s] %s", item->title_id, item->name);
			list_append(list, item);
		}

		if (ptr < end && *ptr == '\r')
		{
			ptr++;
		}
		if (ptr < end && *ptr == '\n')
		{
			ptr++;
		}
	}

	if (data) free(data);
}

list_t * ReadOnlineList(const char* urlPath)
{
	char url[256];
	list_t *list = list_alloc();

	// PSV save-games (Zip folder)
	snprintf(url, sizeof(url), "%s" "PS1/", urlPath);
	_ReadOnlineListEx(url, SAVE_FLAG_PS1, list);

	// PS2 save-games (Zip folder)
	snprintf(url, sizeof(url), "%sPS2/", urlPath);
	_ReadOnlineListEx(url, SAVE_FLAG_PS2, list);

	if (!list_count(list))
	{
		list_free(list);
		return NULL;
	}

	return list;
}

list_t * ReadTrophyList(const char* userPath)
{
	char filePath[256];
	save_entry_t *item;
	code_entry_t *cmd;
	list_t *list;
/*
	if ((db = open_sqlite_db(userPath)) == NULL)
		return NULL;

	list = list_alloc();

	item = _createSaveEntry(SAVE_FLAG_PSV, CHAR_ICON_COPY " Bulk Trophy Management");
	item->type = FILE_TYPE_MENU;
	item->path = strdup(userPath);
	item->codes = list_alloc();
	//bulk management hack
	item->dir_name = malloc(sizeof(void**));
	((void**)item->dir_name)[0] = list;

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Backup selected Trophies to Backup Storage", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createOptions(2, "Copy Trophies to Backup Storage", CMD_COPY_TROPHIES_USB);
	list_append(item->codes, cmd);
	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Backup all Trophies to Backup Storage", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createOptions(2, "Copy Trophies to Backup Storage", CMD_COPY_ALL_TROP_USB);
	list_append(item->codes, cmd);
	list_append(list, item);

	for (int i = 0; i < 3; i++)
	{
		snprintf(filePath, sizeof(filePath), "%s" TROPHIES_PATH_USB, dev[i]);
		if (i && dir_exists(filePath) != SUCCESS)
			continue;

		item = _createSaveEntry(SAVE_FLAG_PSV | SAVE_FLAG_TROPHY, CHAR_ICON_COPY " Import Trophies");
		asprintf(&item->path, "%s" TROPHIES_PATH_USB, dev[i]);
		asprintf(&item->title_id, " %s", dev[i]);
		item->type = FILE_TYPE_MENU;
		list_append(list, item);
	}

	sqlite3_create_collation(db, "trophy_collator", SQLITE_UTF8, NULL, &sqlite_trophy_collate);
	int rc = sqlite3_prepare_v2(db, "SELECT id, npcommid, title FROM tbl_trophy_title WHERE status = 0", -1, &res, NULL);
	if (rc != SQLITE_OK)
	{
		LOG("Failed to fetch data: %s", sqlite3_errmsg(db));
		sqlite3_close(db);
		return NULL;
	}

	while (sqlite3_step(res) == SQLITE_ROW)
	{
		item = _createSaveEntry(SAVE_FLAG_PSV | SAVE_FLAG_TROPHY | SAVE_FLAG_HDD, (const char*) sqlite3_column_text(res, 2));
		item->blocks = sqlite3_column_int(res, 0);
		item->title_id = strdup((const char*) sqlite3_column_text(res, 1));
		asprintf(&item->path, TROPHY_PATH_HDD "data/%s/", apollo_config.user_id, item->title_id);
		item->type = FILE_TYPE_TRP;

		LOG("[%s] F(%X) name '%s'", item->title_id, item->flags, item->name);
		list_append(list, item);
	}

	sqlite3_finalize(res);
	sqlite3_close(db);
*/
	return list;
}

int get_save_details(const save_entry_t* save, char **details)
{
	char sfoPath[256];
	sdslot_dat_t* sdslot;
	size_t size;

	if (save->flags & SAVE_FLAG_PSP || save->flags & SAVE_FLAG_PS1)
	{
		snprintf(sfoPath, sizeof(sfoPath), "%sPARAM.SFO", save->path);
		LOG("Save Details :: Reading %s...", sfoPath);

		sfo_context_t* sfo = sfo_alloc();
		if (sfo_read(sfo, sfoPath) < 0) {
			LOG("Unable to read from '%s'", sfoPath);
			sfo_free(sfo);
			return 0;
		}

		asprintf(details, "%s\n----- PSP Save -----\n"
			"Game: %s\n"
			"Title ID: %s\n"
			"Folder: %s\n"
			"Title: %s\n"
			"Details: %s\n",
			save->path,
			save->name,
			save->title_id,
			save->dir_name,
			(char*)sfo_get_param_value(sfo, "SAVEDATA_TITLE"),
			(char*)sfo_get_param_value(sfo, "SAVEDATA_DETAIL"));

		sfo_free(sfo);
		return 1;
	}

	if (!(save->flags & SAVE_FLAG_PSP))
	{
		asprintf(details, "%s\n\nTitle: %s\n", save->path, save->name);
		return 1;
	}
/*
	if (save->flags & SAVE_FLAG_TROPHY)
	{
		snprintf(sfoPath, sizeof(sfoPath), TROPHY_PATH_HDD "db/trophy_local.db", apollo_config.user_id);
		if ((db = open_sqlite_db(sfoPath)) == NULL)
			return 0;

		char* query = sqlite3_mprintf("SELECT id, description, trophy_num, unlocked_trophy_num, progress,"
			"platinum_num, unlocked_platinum_num, gold_num, unlocked_gold_num, silver_num, unlocked_silver_num,"
			"bronze_num, unlocked_bronze_num FROM tbl_trophy_title WHERE id = %d", save->blocks);

		if (sqlite3_prepare_v2(db, query, -1, &res, NULL) != SQLITE_OK || sqlite3_step(res) != SQLITE_ROW)
		{
			LOG("Failed to fetch data: %s", sqlite3_errmsg(db));
			sqlite3_free(query);
			sqlite3_close(db);
			return 0;
		}

		asprintf(details, "Trophy-Set Details\n\n"
			"Title: %s\n"
			"Description: %s\n"
			"NP Comm ID: %s\n"
			"Progress: %d/%d - %d%%\n"
			"%c Platinum: %d/%d\n"
			"%c Gold: %d/%d\n"
			"%c Silver: %d/%d\n"
			"%c Bronze: %d/%d\n",
			save->name, sqlite3_column_text(res, 1), save->title_id,
			sqlite3_column_int(res, 3), sqlite3_column_int(res, 2), sqlite3_column_int(res, 4),
			CHAR_TRP_PLATINUM, sqlite3_column_int(res, 6), sqlite3_column_int(res, 5),
			CHAR_TRP_GOLD, sqlite3_column_int(res, 8), sqlite3_column_int(res, 7),
			CHAR_TRP_SILVER, sqlite3_column_int(res, 10), sqlite3_column_int(res, 9),
			CHAR_TRP_BRONZE, sqlite3_column_int(res, 12), sqlite3_column_int(res, 11));

		sqlite3_free(query);
		sqlite3_finalize(res);
		sqlite3_close(db);

		return 1;
	}

	if(save->flags & SAVE_FLAG_LOCKED)
	{
		asprintf(details, "%s\n\n"
			"Title ID: %s\n"
			"Dir Name: %s\n"
			"Blocks: %d\n"
			"Account ID: %.16s\n",
			save->path,
			save->title_id,
			save->dir_name,
			save->blocks,
			save->path + 23);

		return 1;
	}
*/

	snprintf(sfoPath, sizeof(sfoPath), "%ssce_sys/param.sfo", save->path);
	LOG("Save Details :: Reading %s...", sfoPath);

	sfo_context_t* sfo = sfo_alloc();
	if (sfo_read(sfo, sfoPath) < 0) {
		LOG("Unable to read from '%s'", sfoPath);
		sfo_free(sfo);
		return 0;
	}

	strcpy(strrchr(sfoPath, '/'), "/sdslot.dat");
	LOG("Save Details :: Reading %s...", sfoPath);
	if (read_buffer(sfoPath, (uint8_t**) &sdslot, &size) != SUCCESS) {
		LOG("Unable to read from '%s'", sfoPath);
		sfo_free(sfo);
		return 0;
	}

	if (sdslot->header.magic == 0x4C534453)
		memcpy(sfoPath, sdslot->header.active_slots, sizeof(sfoPath));
	else
		memset(sfoPath, 0, sizeof(sfoPath));

	char* out = *details = (char*) sdslot;
	uint64_t* account_id = (uint64_t*) sfo_get_param_value(sfo, "ACCOUNT_ID");

	out += sprintf(out, "%s\n----- Save -----\n"
		"Title: %s\n"
		"Title ID: %s\n"
		"Dir Name: %s\n"
		"Account ID: %016llx\n",
		save->path, save->name,
		save->title_id,
		save->dir_name,
		*account_id);

	for (int i = 0; (i < 256) && sfoPath[i]; i++)
	{
		out += sprintf(out, "----- Slot %03d -----\n"
			"Date: %d/%02d/%02d %02d:%02d:%02d\n"
			"Title: %s\n"
			"Subtitle: %s\n"
			"Details: %s\n",
			(i+1), sdslot->slots[i].year, sdslot->slots[i].month, sdslot->slots[i].day,
			sdslot->slots[i].hour, sdslot->slots[i].min, sdslot->slots[i].sec,
			sdslot->slots[i].title,
			sdslot->slots[i].subtitle,
			sdslot->slots[i].description);
	}

	sfo_free(sfo);
	return 1;
}
