// The MIT License (MIT)
// 
// Copyright (c) 2016 Trevor Bakker 
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.



//-------------------------------------------------------------------------------------------------
// Notes and Disclaimers For Developers
// ------------------------------------------------------------------------------------------------

// The requirement was to have one block (block 277) to be our free inode map, but our disk image
// size is 65MB and our one block is size 1kB so it is impossible to map to all 65M entries or bits
// to our 8k bit block therefore necessitating a free block array which offset our original 
// starting data block 278 by the v

//-------------------------------------------------------------------------------------------------
// Includes & Defines
// ------------------------------------------------------------------------------------------------

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <time.h>
#include <inttypes.h>

// MavShell Defines
#define WHITESPACE " \t\n"     				// We want to split our command line up into tokens
                                			// so we need to define what delimits our tokens.
                                			// In this case  white space
                                			// will separate the tokens on our command line
#define MAX_COMMAND_SIZE 255   				// The maximum command-line size
#define MAX_NUM_ARGUMENTS 5    				// Mav shell only supports 10 arguments

// File System Defines
#define BLOCK_SIZE 1024 				// Size of Each Block
#define NUM_BLOCKS 65536 				// Max Number of Blocks in File System 
#define BLOCKS_PER_FILE 1024				// Max File Size is 2^20 bytes and each block is 2^10 bytes 
							// so 2^20/2^10 = 2^10 blocks
#define MAX_FILES 256					// Requirements of the Assignment
#define FIRST_DATA_BLOCK 350				// 278 Starting Requirement  + 512 for Free Block Map... 
#define MAX_FILE_SIZE BLOCK_SIZE * BLOCKS_PER_FILE 	// Can we do Block_Size * Blocks_Per_File ?? 

#define HIDDEN 0x1
#define READONLY 0x2

//-------------------------------------------------------------------------------------------------
// Global Variables & Structures
// ------------------------------------------------------------------------------------------------


// Data Structure of 67 MB Disk Image
uint8_t data[NUM_BLOCKS][BLOCK_SIZE];

// 64 blocks just for free block map // How I get 64 blocks?
// Out Free_block array will have 65536 Entries and each entry is 1 byte each
// (NUM_BLOCKS  / sizeof(Block) = number of free blocks
// 65536 / 1024 = 64 ... 
uint8_t * free_blocks;
uint8_t * free_inodes;

// Directory Structure
struct directoryEntry
{
   char     filename[64];
   short    in_use;
   int32_t  inode;
};

struct directoryEntry * directory;

// inode Structure
struct inode
{
   int32_t  blocks[BLOCKS_PER_FILE];
   short    in_use;
   uint8_t  attribute;			// Attributes of the file
   uint32_t file_size;
   time_t   t;
};

struct inode * inodes;

FILE     *fp;
char     image_name[64];
uint8_t  image_open;		// Bool Value if the disk image is open

//-------------------------------------------------------------------------------------------------
// Light Functions
// ------------------------------------------------------------------------------------------------

// Used in insert to find a free block 
int32_t findFreeBlock()
{
   // The reason we search through zero to NUM_BlOCKS is 
   // because we are just seaching the the free blocks array and
   // not the data array and our actual data blocks and 
   // our index have an offset of FIRST_DATA_BLOCK
   for (int i = 0; i < NUM_BLOCKS; i++)
   {
      if ( free_blocks[i] )
      {
         return i + FIRST_DATA_BLOCK;
      }
   }
   return -1;

}

int32_t findFreeInode()
{
   for (int i = 0; i < MAX_FILES; i++)
   {
      if ( free_inodes[i] )
      {
         return i;
      }
   }
   return -1;

}

int32_t findFreeInodeBlock( int32_t inode )
{
   for (int i = 0; i < BLOCKS_PER_FILE; i++)
   {
      if ( inodes[inode].blocks[i] == -1)
      {
         return i;
      }
   }
   return -1;

}

void init( )
{
   //Pointing the Pointers to the right spot in our disk image
   directory 	= (struct directoryEntry*) &data[0][0]; 
   free_inodes 	= (uint8_t *) &data[19][0];
   inodes    	= (struct inode*) &data[20][0];
   free_blocks 	= (uint8_t *) &data[277][0];

   memset( image_name, 0, 64 ); // Initializing the disk image name to zero
   image_open = 0;		// Disk image is not open 

   // Every file initialization
   for (int i = 0; i < MAX_FILES; i++)
   {
      directory[i].in_use = 0;		// Marking File as not used
      directory[i].inode = -1;		// Pointer to inode is nothing...
      free_inodes[i] = 1;

      memset( directory[i].filename, 0, 64 );	// Initializing the filenames to zero

      //For Every Block in Every File
      for (int j = 0; j < BLOCKS_PER_FILE; j++)
      {
         inodes[i].blocks[j] = -1;	// Pointer to inode is nothing...
         inodes[i].in_use = 0; 		// Marking inode as not used
         inodes[i].attribute = 0;
         inodes[i].file_size = 0;
         inodes[i].t = 0;
      }
   }

   for (int j = 0; j < NUM_BLOCKS; j++)
   {
      free_blocks[j] = 1;
   }

}

uint32_t df()
{
   int count = 0;
   // look at all our data blocks
   for (int j = FIRST_DATA_BLOCK; j < NUM_BLOCKS; j++)
   {
      // if a datablock is free then count it
      if (free_blocks[j-FIRST_DATA_BLOCK])
      {
         count++;
      }
   }
   
   return count * BLOCK_SIZE;
}

void createfs( char* diskName )
{
   fp = fopen ( diskName, "w" );

   strncpy( image_name, diskName, strlen(diskName) );		// Copying diskname to our image_name variable

   memset( data, 0, NUM_BLOCKS * BLOCK_SIZE );			// Allocating Memory Space for Disk Image

   image_open = 1;	// Disk Image is now Open

   // Initializing Every File
   for (int i = 0; i < MAX_FILES; i++)
   {
      directory[i].in_use = 0;		// Marking every file as not used
      directory[i].inode = -1;		// Pointer inodes is nothing
      free_inodes[i] = 1;

      memset( directory[i].filename, 0, 64 ); // Init filenames to zeros

      // Initializing Every Block in Every File
      for (int j = 0; j < BLOCKS_PER_FILE; j++)
      {
         inodes[i].blocks[j]=-1;
         inodes[i].in_use=0;
         inodes[i].attribute=0;
         inodes[i].file_size = 0;
      }
   }
   
   for (int j = 0; j < NUM_BLOCKS; j++)
   {
      free_blocks[j] = 1;
   }

   fclose ( fp ); 	// This makes the closefs pointless
}

void savefs()
{
   if ( image_open == 0 )
   {
      printf("ERROR: Disk image is not open.\n");
   }
   else
   {
   	fp = fopen ( image_name, "w");

   	fwrite( &data[0][0], BLOCK_SIZE, NUM_BLOCKS, fp );

   	memset( image_name, 0, 64);	// Zeroing Disk Image Name out because not using it

   	fclose ( fp );		// Again makes closefs pointless
   }
}

void openfs( char* diskName )
{
   fp = fopen ( diskName, "r");
   
   if (fp == NULL)
   {
	   printf("ERROR: Disk image does not exist\n");
   }
   else
   {
   	strncpy( image_name, diskName, strlen(diskName) );	// Copy the disk image name to our image name variable

   	fread( &data[0][0], BLOCK_SIZE, NUM_BLOCKS, fp );	// Store the data in the disk image to our data structure

   	image_open = 1;		// Mark the disk image as open 

   	fclose ( fp );		// Again makes closefs pointless
   }
}

void closefs()
{
   if ( image_open == 0 )
   {
      printf("ERROR: Disk image is not open.\n");
      return;
   }

   image_open = 0;		// Mark the disk image as closed 
   memset( image_name, 0, 64 );	// Zeroing out Disk Image name becuase not using it
}

void delete( char *filename )
{
   short counter    = 0;         // This is also the index for directory
   short encontrado = 0;         // found
   int32_t inode_index;          // needed to free correct inode

   while(!encontrado && (counter < MAX_FILES))
   {
      // cheking for file
      if ( !strcmp( directory[counter].filename, filename ) )
      {
         encontrado = 1;
         continue;
      }

      counter++;
   }

   if ( !encontrado )
   {
      printf("delete: File not found\n");
   }
   else
   {
      inode_index = directory[counter].inode;   // obtaining the location of inode

      inodes[inode_index].in_use    = 0;        // inode is no longer in use
      free_inodes[inode_index]      = 1;        // inode is now free

      directory[counter].in_use     = 0;        // directory is no longer in use
      free_blocks[counter]          = 1;        // directory is no free
   }

}

void undel( char *filename )
{
   short counter           = 0;         // This is also the index for directory
   short encontrado        = 0;         // found
   int32_t inode_index;                 // needed to free correct inode

   while(!encontrado && (counter < MAX_FILES))
   {
      // cheking for file
      if ( !strcmp( directory[counter].filename, filename ) )
      {
         encontrado = 1;
         continue;
      }

      counter++;
   }

   if ( !encontrado )
   {
      // file does not exist
      printf("undelete: can not find the file.\n");
   }
   else
   {
      directory[counter].in_use     = 1;        // directory is in use
      free_blocks[counter]          = 0;        // directory is no longer free

      inode_index = directory[counter].inode;   // obtaining inode location
            
      inodes[inode_index].in_use    = 1;        // inode is now in use
      free_inodes[inode_index]      = 0;        // directory is not free
   }

}

void list()
{
   int not_found = 1; // boolean to check if a file is found to print message
   for (int i = 0; i < MAX_FILES; i++)
   {
      if (directory[i].in_use && !(inodes[directory[i].inode].attribute & HIDDEN))
      {
         not_found = 0;
         char filename[65];
         memset( filename, 0, 65 );
         strncpy( filename, directory[i].filename, strlen(directory[i].filename) );
         printf("%10s ", filename);
         printf("%8"PRIu32" B     ", inodes[directory[i].inode].file_size);
         printf("%s", ctime(&inodes[directory[i].inode].t));
         
      }
   }
   
   if ( not_found )
   {
      printf("ERROR: No files found.\n");
   }
}

void list_hidden()
{
   int not_found = 1; // boolean to check if a file is found to print message

   for (int i = 0; i < MAX_FILES; i++)
   {
      if (directory[i].in_use)
      {
         not_found = 0;
         char filename[65];
         memset( filename, 0, 65 );
         strncpy( filename, directory[i].filename, strlen(directory[i].filename) );
         printf("%s\n", filename );
      }
   }
   
   if ( not_found )
   {
      printf("ERROR: No files found.\n");
   }
}

void list_attribute()
{
   int not_found = 1; // boolean to check if a file is found to print message

   for (int i = 0; i < MAX_FILES; i++)
   {
      if (directory[i].in_use && !(inodes[directory[i].inode].attribute & HIDDEN))
      {
         not_found = 0;
         char filename[65];
         memset( filename, 0, 65 );
         strncpy( filename, directory[i].filename, strlen(directory[i].filename) );
         printf("%s %8d \n", filename, inodes[directory[i].inode].attribute);
      }
   }
   
   if ( not_found )
   {
      printf("ERROR: No files found.\n");
   }
}

void attribute(char* attribute, char* filename)
{
   uint8_t found = 0;
   struct inode * file_inode;
	for( int i = 0; i < MAX_FILES; i++ )
	{
		if( !strcmp(directory[i].filename, filename))
		{
			found = 1;
			file_inode = &inodes[directory[i].inode];
         break;
      }
   }

   if (found == 0)
   {
      printf("ERROR: No matching file found.\n");
      return;
   }

   if ( !strcmp(attribute, "+h") )
   {
      file_inode->attribute |= HIDDEN;
   }

   else if (!strcmp(attribute, "-h"))
   {
      file_inode->attribute &= ~HIDDEN;
   }

   else if (!strcmp(attribute, "+r"))
   {
      file_inode->attribute |= READONLY;
   }

   else if (!strcmp(attribute, "-r"))
   {
      file_inode->attribute &= ~READONLY;
   }

   else
   {
      printf("ERROR: Incorrect attribute specified.\n");
   }
}

//-------------------------------------------------------------------------------------------------
// Heavy Functions: Insert, Retrieve, Read, Encrypt, & Decrypt
// ------------------------------------------------------------------------------------------------
void readDisk( char* filename, int32_t start_byte, int32_t num_bytes )
{
	uint8_t found = 0;
	for( int i = 0; i < MAX_FILES; i++ )
	{
		if( !strcmp(directory[i].filename, filename))
		{
			found = 1;
			struct inode file_inode = inodes[directory[i].inode];
			FILE* diskFile = fopen( image_name, "r");
			// Now, open the output file that we are going to write the data to.
			if( diskFile == NULL )
			{
				printf("Could not open disk file: %s\n", filename );
				perror("Opening disk file returned");
				return;
			}

			// Initialize our offsets and pointers just we did above when reading from the file.
			uint8_t index_offset = start_byte / BLOCK_SIZE;
			uint16_t remainder = start_byte % BLOCK_SIZE;
			uint32_t block_index = file_inode.blocks[index_offset];
			int32_t read_size   = (int) num_bytes;
			uint32_t offset      = (block_index * BLOCK_SIZE) + remainder;


			printf("Reading %d bytes to %s\n", (int) read_size, filename );

			char buffer[BLOCK_SIZE+1];
			memset( buffer, '\0', BLOCK_SIZE+1);

			// Using copy_size as a count to determine when we've copied enough bytes to the output file.
			// Each time through the loop, except the last time, we will copy BLOCK_SIZE number of bytes from
			// our stored data to the file fp, then we will increment the offset into the file we are writing to.
			// On the last iteration of the loop, instead of copying BLOCK_SIZE number of bytes we just copy
			// how ever much is remaining ( copy_size % BLOCK_SIZE ).  If we just copied BLOCK_SIZE on the
			// last iteration we'd end up with gibberish at the end of our file. 
			while( read_size > 0 )
			{ 
				int temp_num_bytes;

				// If the remaining number of bytes we need to copy is less than BLOCK_SIZE then
				// only copy the amount that remains. If we copied BLOCK_SIZE number of bytes we'd
				// end up with garbage at the end of the file.
				if( (read_size + start_byte)%BLOCK_SIZE  < BLOCK_SIZE )
				{
					if ( read_size + start_byte > file_inode.file_size )
					{
						temp_num_bytes = file_inode.file_size - start_byte;
					}
					else
						temp_num_bytes = read_size;
				}
				else 
				{
					temp_num_bytes = (read_size + start_byte) - BLOCK_SIZE;
				}

				// Read num_bytes number of bytes from our data array into our output file.

				fseek( diskFile, offset, SEEK_SET );
				fread( buffer, temp_num_bytes, 1, diskFile ); 
				char *buff = &buffer[0];
				while(*buff)
				{
					printf("%X", (uint32_t) *buff++);
				}
				printf("\n");

				// Reduce the amount of bytes remaining to copy, increase the offset into the file
				// and increment the block_index to move us to the next data block.
				
				if ( read_size + start_byte > file_inode.file_size )
					read_size -= read_size;
				else
					read_size -= temp_num_bytes;
				offset    += BLOCK_SIZE;
				block_index = file_inode.blocks[++index_offset];

				// Since we've copied from the point pointed to by our current file pointer, increment
				// offset number of bytes so we will be ready to copy to the next area of our output file.
			}

		        // Close the output file, we're done. 
		        fclose( diskFile );
		}
	}
	if(!found)
		printf("ERROR: File not found.\n"); 
}

void retrieve(char* filename, char* newFilename)
{
	uint8_t found = 0;
	for( int i = 0; i < MAX_FILES; i++ )
	{
		if( !strcmp(directory[i].filename, filename))
		{
			found = 1;
			struct inode file_inode = inodes[directory[i].inode];
			FILE* newFile = fopen( newFilename, "w");
			// Now, open the output file that we are going to write the data to.
			if( newFile == NULL )
			{
				printf("Could not open output file: %s\n", filename );
				perror("Opening output file returned");
				return;
			}

			// Initialize our offsets and pointers just we did above when reading from the file.
			uint16_t inode_block_idx = 0;
			uint32_t block_index = file_inode.blocks[inode_block_idx];
			int32_t copy_size   = (int) file_inode.file_size;
			uint32_t offset      = 0;

			printf("Writing %d bytes to %s\n", (int) copy_size, newFilename );

			// Using copy_size as a count to determine when we've copied enough bytes to the output file.
			// Each time through the loop, except the last time, we will copy BLOCK_SIZE number of bytes from
			// our stored data to the file fp, then we will increment the offset into the file we are writing to.
			// On the last iteration of the loop, instead of copying BLOCK_SIZE number of bytes we just copy
			// how ever much is remaining ( copy_size % BLOCK_SIZE ).  If we just copied BLOCK_SIZE on the
			// last iteration we'd end up with gibberish at the end of our file. 
			while( copy_size > 0 )
			{ 
				int num_bytes;

				// If the remaining number of bytes we need to copy is less than BLOCK_SIZE then
				// only copy the amount that remains. If we copied BLOCK_SIZE number of bytes we'd
				// end up with garbage at the end of the file.
				if( copy_size < BLOCK_SIZE )
				{
					num_bytes = copy_size;
				}
				else 
				{
					num_bytes = BLOCK_SIZE;
				}

				// Write num_bytes number of bytes from our data array into our output file.
				fwrite( data[block_index], num_bytes, 1, newFile ); 

				// Reduce the amount of bytes remaining to copy, increase the offset into the file
				// and increment the block_index to move us to the next data block.
				copy_size -= BLOCK_SIZE;
				offset    += BLOCK_SIZE;
				block_index = file_inode.blocks[++inode_block_idx];


				// Since we've copied from the point pointed to by our current file pointer, increment
				// offset number of bytes so we will be ready to copy to the next area of our output file.
				fseek( newFile, offset, SEEK_SET );
			}

		        // Close the output file, we're done. 
		        fclose( newFile );
		}
	}
	if(!found)
		printf("ERROR: File not found.\n"); 

}

void insert( char* filename)
{
   // Verify filename isn't null
   if (filename == NULL)
   {
      printf("ERROR: Filename is NULL.\n");
      return;
   }

   // verify the file exists
   // read man page for stat for more details
   struct stat buf;
   int ret = stat( filename, &buf );

   if ( ret == -1 )
   {
      printf("ERROR: File does not exist.\n");
      return;
   }

   // Verify file isn't too big (10MB Limit)
   if ( buf.st_size > MAX_FILE_SIZE )
   {
      printf("ERROR: File size is too large.\n");
      return;
   }

   // Verify the is enough space
   if ( buf.st_size > df() )
   {
      printf("ERROR: Not enough free disk sapce.\n");
      return;
   }

   // Find empty directory entry
   int directory_entry = -1;
   for (int i = 0; i < MAX_FILES; i++)
   {
      if (directory[i].in_use == 0)
      {
         directory_entry = i;
         break;
      }
   }

   if ( directory_entry == -1)
   {
      printf("ERROR: Could not find a free directory entry.\n");
      return;
   }

   // Open the input file read-only 
    FILE *ifp = fopen ( filename, "r" ); 
    printf("Reading %d bytes from %s\n", (int) buf . st_size, filename );
 
    // Save off the size of the input file since we'll use it in a couple of places and 
    // also initialize our index variables to zero. 
    int32_t copy_size   = buf . st_size;

    // We want to copy and write in chunks of BLOCK_SIZE. So to do this 
    // we are going to use fseek to move along our file stream in chunks of BLOCK_SIZE.
    // We will copy bytes, increment our file pointer by BLOCK_SIZE and repeat.
    int32_t offset      = 0;               

    // We are going to copy and store our file in BLOCK_SIZE chunks instead of one big 
    // memory pool. Why? We are simulating the way the file system stores file data in
    // blocks of space on the disk. block_index will keep us pointing to the area of
    // the area that we will read from or write to.
    int32_t block_index = -1;
   
   // Find a free inode
   int32_t inode_index = findFreeInode();
   if ( inode_index == -1 )
   {
      printf("ERROR: Cannont find free inode.\n");
      return;
   }

   // Place the file into the directory
   directory[directory_entry].in_use = 1;		// Mark File as in use
   directory[directory_entry].inode = inode_index;	// Point to the correct block
   strncpy(directory[directory_entry].filename, filename, strlen( filename )); // copy the filename into the directory entry

   // Inode configurations
   inodes[inode_index].file_size = buf.st_size; // mark the file size of the file
   inodes[inode_index].in_use = 1;  // set the inode of the file in use
   free_inodes[inode_index] = 0;  // update the free inode list
   time_t t;
   inodes[inode_index].t = time(&t);

   // copy_size is initialized to the size of the input file so each loop iteration we
   // will copy BLOCK_SIZE bytes from the file then reduce our copy_size counter by
   // BLOCK_SIZE number of bytes. When copy_size is less than or equal to zero we know
   // we have copied all the data from the input file.
   while( copy_size > 0 )
   {
      fseek( ifp, offset, SEEK_SET );
 
      // Read BLOCK_SIZE number of bytes from the input file and store them in our
      // data array. 

      // Find a free block
      block_index = findFreeBlock();

      if ( block_index == -1 )
      {
         printf("ERROR: Cannont find free block.\n");
         return;
      }
      else 
      {
	      free_blocks[block_index-FIRST_DATA_BLOCK] = 0; // mark the block as in use
      }

      int32_t bytes  = fread( data[block_index], BLOCK_SIZE, 1, ifp );

      //save the block in the inode
      int32_t inode_block = findFreeInodeBlock( inode_index );
      inodes[inode_index].blocks[inode_block] = block_index;


      // If bytes == 0 and we haven't reached the end of the file then something is 
      // wrong. If 0 is returned and we also have the EOF flag set then that is OK.
      // It means we've reached the end of our input file.
      if( bytes == 0 && !feof( ifp ) )
      {
        printf("ERROR: An error occured reading from the input file.\n");
        return;
      }

      // Clear the EOF file flag.
      clearerr( ifp );

      // Reduce copy_size by the BLOCK_SIZE bytes.
      copy_size -= BLOCK_SIZE;
      
      // Increase the offset into our input file by BLOCK_SIZE.  This will allow
      // the fseek at the top of the loop to position us to the correct spot.
      offset    += BLOCK_SIZE;
      
      block_index = findFreeBlock();
    }

    // We are done copying from the input file so close it out.
    fclose( ifp );

   // find free inodes and place file
}

// encryption
void encryption(char *filename, int cipher)
{
   
   // Verify filename is valid and get the directory_index
   int directory_index = -1;

   uint8_t valid= 0;
   for (int i=0; i< MAX_FILES; i++)
   {
    
      if(strcmp(directory[i].filename, filename) == 0)
      {
         directory_index = i;
         valid = 1;
      }
   }
   if(valid == 0)
      {
         printf("ERROR: Filename does not exist.\n");
         return;
      }
  
   // Verify cipher is valid
   if (cipher > 256 || cipher < 0)
   {
      printf("ERROR: Cipher must be between 0 and 255.\n");
      return;
   }


   // get the inode_index from the directory entry
   int32_t inode_index = directory[directory_index].inode;

   // get the file_size from the inode
   int32_t file_size = inodes[inode_index].file_size;


   // get the number of blocks from the file_size using mod 1024
   int32_t number_of_blocks = file_size/1024;
   int leftover = 0;
   //get the left over bytes from the file_size , then leftover = file_size-(number_of_blocks*1024)
   if (file_size%1024 != 0)
   {
      leftover = file_size - (number_of_blocks*1024);
   }

   
   for (int i=0; i< number_of_blocks; i++)
   {
      for (int j=0; j<1024; j++)
      {
         data[inodes[inode_index].blocks[i]][j] = data[inodes[inode_index].blocks[i]][j] ^ cipher;
      }
      
   }
   if(leftover != 0)
   {
      for (int i=0; i< leftover; i++)
      {
         data[inodes[inode_index].blocks[number_of_blocks]][i] = data[inodes[inode_index].blocks[number_of_blocks]][i] ^ cipher;
      }
   }
}

// decryption
void decryption(char *filename, int cipher)
{
   
   // Verify filename is valid and get the directory_index
   int directory_index = -1;

   uint8_t valid= 0;
   for (int i=0; i< MAX_FILES; i++)
   {
    
      if(strcmp(directory[i].filename, filename) == 0)
      {
         directory_index = i;
         valid = 1;
      }
   }
   if(valid == 0)
      {
         printf("ERROR: Filename does not exist.\n");
         return;
      }
  
   // Verify cipher is valid
   if (cipher > 256 || cipher < 0)
   {
      printf("ERROR: Cipher must be between 0 and 255.\n");
      return;
   }


   // get the inode_index from the directory entry
   int32_t inode_index = directory[directory_index].inode;

   // get the file_size from the inode
   int32_t file_size = inodes[inode_index].file_size;


   // get the number of blocks from the file_size using mod 1024
   int32_t number_of_blocks = file_size/1024;
   int leftover = 0;
   //get the left over bytes from the file_size , then leftover = file_size-(number_of_blocks*1024)
   if (file_size%1024 != 0)
   {
      leftover = file_size - (number_of_blocks*1024);
   }

   
   for (int i=0; i< number_of_blocks; i++)
   {
      for (int j=0; j<1024; j++)
      {
         data[inodes[inode_index].blocks[i]][j] = data[inodes[inode_index].blocks[i]][j] ^ cipher;
      }
      
   }
   if(leftover != 0)
   {
      for (int i=0; i< leftover; i++)
      {
         data[inodes[inode_index].blocks[number_of_blocks]][i] = data[inodes[inode_index].blocks[number_of_blocks]][i] ^ cipher;
      }
   }
}


//-------------------------------------------------------------------------------------------------
// Main 
// ------------------------------------------------------------------------------------------------

int main()
{

   char * command_string = ( char* ) malloc( MAX_COMMAND_SIZE );

   fp = NULL;
   
   init();
   
   while( 1 )
   {
      // Print out the msh prompt
      printf ("mfs> ");

       // Read the command from the commandline.  The
       // maximum command that will be read is MAX_COMMAND_SIZE
      // This while command will wait here until the user
      // inputs something since fgets returns NULL when there
      // is no input

      fflush( stdin );
      while( !fgets (command_string, MAX_COMMAND_SIZE, stdin) );
         
      /* Parse input */
      char *token[MAX_NUM_ARGUMENTS];

      for( int i = 0; i < MAX_NUM_ARGUMENTS; i++ )
      {
         token[i] = NULL;
      }

      int   token_count = 0;                                 
                                                            
      // Pointer to point to the token
      // parsed by strsep
      char *argument_ptr = NULL;                                         
                                                            
      char *working_string  = strdup( command_string );                

      if ((working_string[0] == '\n') || (working_string[0] == '\0') || (working_string == NULL))
      {
         continue;
      }
      
      // we are going to move the working_string pointer so
      // keep track of its original value so we can deallocate
      // the correct amount at the end
      char *head_ptr = working_string;

      // Tokenize the input strings with whitespace used as the delimiter
      while ( ((argument_ptr = strsep(&working_string, WHITESPACE))!= NULL) && 
               (token_count<MAX_NUM_ARGUMENTS))
      {
         token[token_count] = strndup( argument_ptr, MAX_COMMAND_SIZE );
         if( strlen( token[token_count] ) == 0 )
         {
         token[token_count] = NULL;
         }
         token_count++;
      }

      // Process filesystem commands

      // "createfs"
      if ( token[0] != NULL && !(strcmp(token[0], "createfs")) )
      {
         if (token[1] == NULL)
         {
            printf("ERROR: No disk image name specified.\n");
            continue;
         }
         createfs( token[1] );
      }

      // "savefs"
      if ( token[0] != NULL && !(strcmp(token[0], "savefs")) )
      {
         savefs( );
      }
      
      // "open"
      if ( token[0] != NULL && !(strcmp(token[0], "open")) )
      {
         if (token[1] == NULL)
         {
            printf("ERROR: No disk image name specified.\n");
            continue;
         }
         openfs( token[1] );
      }

      // "close"
      if ( token[0] != NULL && !(strcmp(token[0], "close")) )
      {
         closefs( );
      }

            // "list"
      if ( token[0] != NULL && !(strcmp(token[0], "list")) )
      {
         if ( !image_open )
         {
            printf("ERROR: Disk image is not open.\n");
            continue;
         }

         if (token[1] == NULL)
         {
            list();
            continue;
         }

         if ( !(strcmp(token[1], "-h")) )
         {
            list_hidden();
            continue;
         }

         else if ( !( strcmp(token[1], "-a")) )
         {
            list_attribute();
            continue;
         }

         else
         {
            list();
            continue;
         }
      }

      // "attrib"
      if ( token[0] != NULL && !(strcmp(token[0], "attrib")) )
      {
         if ( !image_open )
         {
            printf("ERROR: Disk image is not open.\n");
            continue;
         }

         if ( token[1] == NULL)
         {
            printf("ERROR: Attribute not specified.\n");
            continue;
         }

         if ( token[2] == NULL)
         {
            printf("ERROR: Filename not specified.\n");
            continue;
         }

         attribute(token[1], token[2]);
      }

      // "df"
      if ( token[0] != NULL && !(strcmp(token[0], "df")) )
      {
         if ( !image_open )
         {
            printf("ERROR: Disk image is not open.\n");
            continue;
         }

         
         printf("%d bytes free\n", df() );
      }

       // "quit"
      if ( token[0] != NULL && !(strcmp(token[0], "quit")) )
      {
         _exit(0);
      }

      // "insert"
      if ( token[0] != NULL && !(strcmp(token[0], "insert")) )
      {
         if ( !image_open)
         {
            printf("ERROR: Disk image not open.\n");
            continue;
         }

         if (token[1] == NULL)
         {
            printf("ERROR: No filename specified.\n");
            continue;
         }

         insert ( token[1] );
      }

      // "retrieve"
      if ( token[0] != NULL && !(strcmp(token[0], "retrieve")) )
      {
         if ( !image_open)
         {
            printf("ERROR: Disk image not open.\n");
            continue;
         }

         if (token[1] == NULL)
         {
            printf("ERROR: No filename specified.\n");
            continue;
         }
	      
         if (token[2] == NULL)
         	retrieve ( token[1], token[1] );
	      
         else 
		      retrieve( token[1], token[2] );
      }

      // "read"
      if ( token[0] != NULL && !(strcmp(token[0], "read")) )
      {
         if ( !image_open)
         {
            printf("ERROR: Disk image not open.\n");
            continue;
         }

         if (token[1] == NULL)
         {
            printf("ERROR: No filename specified.\n");
            continue;
         }
	 
         if (token[2] == NULL)
         {
            printf("ERROR: No start byte specified.\n");
            continue;
         }
      
         if (token[3] == NULL)
         {
            printf("ERROR: No start byte specified.\n");
            continue;
         }
         
         readDisk(token[1], atoi(token[2]), atoi(token[3]));
      }

      // encryption
      if ( token[0] != NULL && !(strcmp(token[0], "encrypt")) )
      {
         if ( !image_open)
         {
            printf("ERROR: Disk image not open.\n");
            continue;
         }

         if (token[1] == NULL)
         {
            printf("ERROR: No filename specified.\n");
            continue;
         }
         printf("%s\n", token[1]);

         if (token[2] == NULL)
         {
            printf("ERROR: No cipher specified.\n");
            continue;
         }

         int cipher = atoi(token[2]);
         encryption( token[1], cipher );
      }

      // decryption
      if ( token[0] != NULL && !(strcmp(token[0], "decrypt")) )
      {
         if ( !image_open)
         {
            printf("ERROR: Disk image not open.\n");
            continue;
         }

         if (token[1] == NULL)
         {
            printf("ERROR: No filename specified.\n");
            continue;
         }
         printf("%s\n", token[1]);

         if (token[2] == NULL)
         {
            printf("ERROR: No cipher specified.\n");
            continue;
         }
         int cipher = atoi(token[2]);
         decryption( token[1], cipher );
      }

      // "delete"
      if ( token[0] != NULL && !(strcmp(token[0], "delete")) )
      {
         if ( !image_open)
         {
            printf("ERROR: Disk image not open.\n");
            continue;
         }

         if (token[1] == NULL)
         {
            printf("ERROR: No filename specified.\n");
            continue;
         }

         delete ( token[1] );
      }

      // "undel"
      if ( token[0] != NULL && !(strcmp(token[0], "undel")) )
      {
         if ( !image_open)
         {
            printf("ERROR: Disk image not open.\n");
            continue;
         }

         if (token[1] == NULL)
         {
            printf("ERROR: No filename specified.\n");
            continue;
         }

         undel ( token[1] );
      } 

      // Cleanup allocated memory
      for( int i = 0; i < MAX_NUM_ARGUMENTS; i++ )
      {
         if( token[i] != NULL )
         {
         free( token[i] );
         }
      }

      free( head_ptr );

   }

  free( command_string );

  return 0;
  // e2520ca2-76f3-90d6-0242ac120003
}
