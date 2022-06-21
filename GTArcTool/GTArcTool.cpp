// GTArcTool.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <stdlib.h>
#include <string.h>

#define DEFAULT_FILE_EXT "bin"

#define GT_FORMAT_MAGIC 0x29232840
#define GTSEQ_MAGIC 0x47514553
#define GTINST_MAGIC 0x54534E49
#define GTENGN_MAGIC 0x4E474E45

#define GT_FORMAT_MAGIC_TEXT "@(#)"
#define GTARC_MAGIC_STR "GT-ARC"
#define GTCAR_MAGIC_STR "GT-CAR"
#define USEDCAR_MAGIC_STR "USEDCAR"

struct GTArcHead
{
	unsigned int Magic1; // always @(#) // 40 28 23 29 // 0x29232840
	char Magic2[6]; // GT-ARC
	short int unk2;
	short int unk3; // version?
	short int filecount;
	// after this are file entries...
}ArcHeader;

struct GTArcFile
{
	unsigned int offset;
	unsigned int size;
	unsigned int unk; // some sort of secondary size or file type?
}*ArcFile;

char IniOutPath[_MAX_PATH];
char FileOutPath[_MAX_PATH];
char FileInPath[_MAX_PATH];
char MkDirStr[_MAX_PATH + 7];
char OutputDir[_MAX_PATH];
char* BaseFilename;
void* FileBuffer;
bool bFileList = true;

//char* WriterFiletypeArray;
char** InFilenames;

bool bCheckDirExistance(const char* dirpath)
{
	struct stat st = { 0 };
	if (stat(dirpath, &st) == -1)
		return false;
	return true;
}

unsigned int CountLinesInFile(FILE *finput)
{
	unsigned long int OldOffset = ftell(finput);
	unsigned int LineCount = 0;
	char ReadCh;
	fseek(finput, 0, SEEK_SET);

	while (!feof(finput))
	{
		ReadCh = fgetc(finput);
		if (ReadCh == '\n')
			LineCount++;
	}
	fseek(finput, OldOffset, SEEK_SET);
	return LineCount + 1;
}

char* TryToDetectFileExt(void* inbuffer)
{
	if (*(unsigned int*)inbuffer == GTSEQ_MAGIC) // SEQG
	{
		return "seq";
	}
	if (*(unsigned int*)inbuffer == GTINST_MAGIC) // INST
	{
		return "ins";
	}
	if (*(unsigned int*)inbuffer == GTENGN_MAGIC) // ENGN
	{
		return "es";
	}
	if (*(unsigned int*)inbuffer == GT_FORMAT_MAGIC) // GT-something format
	{
		if (strcmp((char*)((unsigned int)inbuffer + 4), GTARC_MAGIC_STR) == 0)
			return "arc";
		if (strcmp((char*)((unsigned int)inbuffer + 4), GTCAR_MAGIC_STR) == 0)
			return "car";
		if (strcmp((char*)((unsigned int)inbuffer + 4), USEDCAR_MAGIC_STR) == 0)
			return "usc";
	}

	return DEFAULT_FILE_EXT;
}

unsigned int GetFileSize(const char* FileName)
{
	struct stat st;
	stat(FileName, &st);
	return st.st_size;
}


int WriteGTArc(char* IniFilePath, char* OutFilePath)
{
	printf("Opening & parsing: %s\n\n", IniFilePath);

	FILE *fin = fopen(IniFilePath, "r");
	FILE *fout;
	int dummy;
	//int RelativeStart = 0;
	int Cursor = 0;

	if (!fin)
	{
		printf("ERROR: Can't open %s\n", IniFilePath);
		perror("ERROR");
		return -1;
	}

	//ArcHeader.unk1 = 0x40;
	ArcHeader.Magic1 = GT_FORMAT_MAGIC;
	strcpy(ArcHeader.Magic2, GTARC_MAGIC_STR);
	
	fscanf(fin, "[HEADER]\nFileCount = %d\nUnk2 = %hX\nUnk3 = %hX\n\n", &ArcHeader.filecount, &ArcHeader.unk2, &ArcHeader.unk3);
	
	InFilenames = (char**)calloc(ArcHeader.filecount, sizeof(char*));
	ArcFile = (GTArcFile*)calloc(ArcHeader.filecount, sizeof(GTArcFile));

	//RelativeStart = sizeof(GTArcFile) * ArcHeader.filecount + sizeof(GTArcHead);
	Cursor = sizeof(GTArcFile) * ArcHeader.filecount + sizeof(GTArcHead);;

	// get info from the .ini file
	for (unsigned int i = 0; i < ArcHeader.filecount; i++)
	{
		fscanf(fin, "[%d]\nPath = %s\nUnk = %X\n\n", &dummy, FileOutPath, &ArcFile[i].unk);
		strcpy(FileInPath, FileOutPath);
		InFilenames[i] = (char*)calloc(strlen(FileInPath) + 1, sizeof(char));
		strcpy(InFilenames[i], FileInPath);

		ArcFile[i].size = GetFileSize(FileInPath);
		ArcFile[i].offset = Cursor;
		Cursor += ArcFile[i].size;
	}
	fclose(fin);

	// once we've gathered the necessary info we build from it...
	printf("Creating new GT-ARC file: %s\n", OutFilePath);
	fout = fopen(OutFilePath, "wb");
	if (!fout)
	{
		printf("ERROR: Can't open %s\n", OutFilePath);
		perror("ERROR");
		return -1;
	}

	fwrite(&ArcHeader, sizeof(GTArcHead), 1, fout);
	fwrite(ArcFile, sizeof(GTArcFile), ArcHeader.filecount, fout);

	//for (unsigned int i = 0; i < ArcHeader.filecount; i++)
	//{
	//	fwrite(&ArcFile[i], sizeof(GTArcHead), 1, fout);
	//}

	for (unsigned int i = 0; i < ArcHeader.filecount; i++)
	{
		printf("Opening & writing: %s\n", InFilenames[i]);
		fin = fopen(InFilenames[i], "rb");
		if (!fin)
		{
			printf("ERROR: Can't open %s\n", InFilenames[i]);
			perror("ERROR");
			return -1;
		}

		FileBuffer = malloc(ArcFile[i].size);

		fread(FileBuffer, ArcFile[i].size, 1, fin);
		fwrite(FileBuffer, ArcFile[i].size, 1, fout);

		free(FileBuffer);
		fclose(fin);
	}

	fclose(fout);
	return 0;
}

int ExtractGTArc(char* InFilePath, char* OutFilePath, char* FilenameListPath)
{
	printf("Opening: %s\n\n", InFilePath);
	
	FILE *fin = fopen(InFilePath, "rb");
	FILE *fout;
	FILE *inifile;
	FILE *namelist;

	char* InFilename;
	unsigned long OldPosition = 0;
	unsigned int FileListCount = 0;
	//char CurrentFileType = 0;

	if (!fin)
	{
		printf("ERROR: Can't open %s\n", InFilePath);
		perror("ERROR");
		return -1;
	}

	strcpy(IniOutPath, OutFilePath);
	InFilename = strrchr(InFilePath, '\\');
	if (InFilename == NULL)
		InFilename = InFilePath;
	else
		InFilename += 1;

	if (!bCheckDirExistance(OutFilePath))
	{
		printf("Creating directory: %s\n", OutFilePath);
		sprintf(MkDirStr, "mkdir \"%s\"", OutFilePath);
		system(MkDirStr);
	}

	// ini file stuff
	strcat(IniOutPath, "\\");
	strcat(IniOutPath, InFilename);
	strcpy(strrchr(IniOutPath, '.'), ".ini");
	inifile = fopen(IniOutPath, "w");
	if (!inifile)
	{
		printf("ERROR: Can't open %s\n", IniOutPath);
		perror("ERROR");
		return -1;
	}

	fread(&ArcHeader, sizeof(GTArcHead), 1, fin);

	if (ArcHeader.Magic1 != GT_FORMAT_MAGIC)
	{
		printf("ERROR: Wrong header magic! GT format files must start with %s\n", GT_FORMAT_MAGIC_TEXT);
		return -1;
	}

	printf("HEADER INFO:\nMagic: %s\nFile count: %d\nUnk: 0x%hX\n\n", ArcHeader.Magic2, ArcHeader.filecount, ArcHeader.unk3);

	if (strcmp(ArcHeader.Magic2, GTARC_MAGIC_STR) != 0)
	{
		printf("ERROR: Wrong header magic! This tool only supports %s\n", GTARC_MAGIC_STR);
		return -1;
	}

	fprintf(inifile, "[HEADER]\nFileCount = %d\nUnk2 = %hX\nUnk3 = %hX\n\n", ArcHeader.filecount, ArcHeader.unk2, ArcHeader.unk3);
	
	ArcFile = (GTArcFile*)calloc(ArcHeader.filecount, sizeof(GTArcFile));

	if (bFileList)
	{
		printf("Opening & parsing filename list: %s\n", FilenameListPath);
		namelist = fopen(FilenameListPath, "r");
		if (!namelist)
		{
			printf("ERROR: Can't open %s\n", FilenameListPath);
			perror("ERROR");
			return -1;
		}
		FileListCount = CountLinesInFile(namelist);
		InFilenames = (char**)calloc(FileListCount, sizeof(char*));
		for (unsigned int i = 0; i < FileListCount; i++)
		{
			fgets(FileInPath, _MAX_PATH, namelist);
			if (FileInPath[strlen(FileInPath) - 1] == '\n')
				FileInPath[strlen(FileInPath) - 1] = 0;
			InFilenames[i] = (char*)calloc(strlen(FileInPath), sizeof(char));
			strcpy(InFilenames[i], FileInPath);
		}
		fclose(namelist);
	}
	
	printf("FILE INFO:");
	
	for (unsigned int i = 0; i < ArcHeader.filecount; i++)
	{
		fread(&ArcFile[i], sizeof(GTArcFile), 1, fin);
		OldPosition = ftell(fin);

		fseek(fin, ArcFile[i].offset, SEEK_SET);

		printf("\nFile: %d\nOffset: 0x%X\nSize: 0x%X\nUnk: 0x%X\n", i, ArcFile[i].offset, ArcFile[i].size, ArcFile[i].unk);

		
		// malloc & read
		FileBuffer = malloc(ArcFile[i].size);
		fseek(fin, ArcFile[i].offset, SEEK_SET);
		fread(FileBuffer, ArcFile[i].size, 1, fin);
		fseek(fin, OldPosition, SEEK_SET);

		// filename generation + outfile write
		if (bFileList && i <= FileListCount)
			sprintf(FileOutPath, "%s\\%s", OutFilePath, InFilenames[i]);
		else
			sprintf(FileOutPath, "%s\\%d.%s", OutFilePath, i, TryToDetectFileExt(FileBuffer));

		fout = fopen(FileOutPath, "wb");
		if (!fout)
		{
			printf("ERROR: Can't open %s\n", FileOutPath);
			perror("ERROR");
			return -1;
		}
		fwrite(FileBuffer, ArcFile[i].size, 1, fout);

		// free everything
		free(FileBuffer);
		fclose(fout);

		// write file entry in the ini file
		fprintf(inifile, "[%d]\nPath = %s\nUnk = %X\n\n", i, FileOutPath + strlen(OutFilePath) + 1, ArcFile[i].unk);
	}

	fclose(inifile);
	fclose(fin);
	return 0;
}

int main(int argc, char *argv[])
{
	printf("Polyphony Digital GT-ARC tool\n");
	if (argc < 2)
	{
		printf("ERROR: Too few arguments.\nUSAGE: %s GTArc [OutPath NamesList]\nWRITE MODE USAGE: %s -w ArcIniFile OutArcFile", argv[0], argv[0]);
		return -1;
	}

	if (argv[1][0] == '-')
	{
		if (argv[1][1] == 'w')
		{
			printf("Going to write mode...\n");
			if (argc < 4)
			{
				printf("ERROR: Too few arguments.\nWRITE MODE USAGE: %s -w ArcIniFile OutArcFile", argv[0]);
				return -1;
			}
			WriteGTArc(argv[2], argv[3]);
			return 0;
		}
	}

	if (argv[2] == NULL)
	{
		strcpy(OutputDir, argv[1]);
		BaseFilename = strrchr(OutputDir, '.');
		if (BaseFilename != NULL)
			BaseFilename[0] = 0;
	}
	else
		strcpy(OutputDir, argv[2]);

	if (argc < 4)
		bFileList = false;

	ExtractGTArc(argv[1], OutputDir, argv[3]);

    return 0;
}

