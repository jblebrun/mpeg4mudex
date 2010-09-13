/***
 * Removes the meta boxes from the provided mpeg4 source file, writing the result
 * out to a new file given the provided filename.
 *
 * This sample program only works for media files that stay within 32-bit box/file sizes.
 * If the file is larger than the 32-bit maximum, the following additions could be made:
 *
 * 1. Check for 64-bit boxes size encoding (atom size=1) and adjust according.
 * 2. Find the co64 table instead of stco table, and modify those table entries.
 */
#include "stdio.h"
#include "stdint.h"
#include "stdlib.h"
#include "stddef.h"
#include "string.h"
#include <vector>

/* M4A atoms can be either data holders, or containers of other
 * atoms. Actually, it's slightly more complicated than that, since there
 * are a few choice atoms that are containers that also have data inside of them.
 * However, we don't need to worry about this in this utility, since the only
 * container that has that property is the "meta" container, but we're just
 * removing it, anyway; we don't need to recurse into it.
 *
 * According to the docs for the m4a file type, we can find meta in the following
 * places in the hierarchy:
 * meta
 * moov.udta.meta
 * moov.udta.trak.meta
 *
 * So, based on that information, we track make sure to check the 
 * substructure of the necessary containers. 
 */
const char *const containers_of_interest = "moov|udta|trak|mdia|minf|stbl";
typedef struct atom_t {
    atom_t* parent;
    union byte_addressable {
        uint64_t word;
        uint8_t bytes[8];
    } len;
    char name[5];
    uint64_t data_size;
    int64_t data_remaining;
    unsigned char* data;
    bool container;
    std::vector<atom_t*> children;
} atom_t;

/***
 * Find the next box (atom) in the provided m4a file
 *
 * Allocates memory for the atom if necessary, and returns 
 * a new atom_t with information about the new atom.
 */
atom_t* get_next_box(FILE* m4a_file) {
    atom_t *atom = (atom_t*)malloc(sizeof(atom_t));
    int i;
   
    /* Read in big-endian order */
    
    //atom->len.word = 0;
    //for(i = 3; i >= 0; i--) {
    //    fread(&(atom->len.bytes[i]), 1, 1, m4a_file);
    //}
    
    fread(atom->name, 1, 4, m4a_file);
   
    /* If the standard length word is 1, then we
     * expect an 8-byte length immediately follow
     * the name.
     *
     * Also the header is effectively 16 bytes now. */ 
    if(atom->len.word == 1) {
        /* Read in big-endian order */
        for(i = 7; i >= 0; i--) {
            fread(&(atom->len.bytes[i]), 1, 1, m4a_file);
        }
        atom->data_size = atom->len.word - 16;
    } else {
        atom->data_size = atom->len.word - 8;
    }

    /* Initialize the struct depending on whether 
     * it's a container of interest or just a 
     * data blob to pass through.
     */
    if(strstr(containers_of_interest, atom->name) != 0) {
        //If it's a container, mark the size in data_remaining so the main loop
        //knows how much to process
        atom->data = NULL;
        atom->data_remaining = atom->data_size;
    } else {
        //Otherwise, just throw the data in a char blob
        //to dump back out later
        atom->data = (unsigned char*)malloc(atom->data_size);;
        fread(atom->data, atom->data_size, 1, m4a_file);
        atom->data_remaining = 0;
    }
    return atom;
}

// A little function to look for meta tags in a less structured way
// Just used for testing
// Could possibly result in a false positive if the "meta" tag appears 
// in binary data by chance, although my hunch is that this is unlikely.
int find_meta(FILE *m4a_file) {
    const char* target = "meta";
    unsigned int tmp;
    int i;
    for(i = 0; i < strlen(target); i++) {
        tmp <<= 8;
        tmp |= (unsigned char)target[i];
    }
    const unsigned int target_checksum = tmp; //Sum of characters in "meta"
    int checksum = 0;
    unsigned char c = 0;
    unsigned char buffer[8] = {0,0,0,0,0,0,0,0}; 
    int index = 0;
    bool found = false;
    while(fread(&c, 1, 1, m4a_file) > 0) {
        index++;
        buffer[index & 7] = c;
        checksum <<= 8;
        checksum |= c;
        if(checksum == target_checksum) {
            found = true;
            for(i = strlen(target)-1; i >=0; i--) {
                if(target[i] != buffer[(index+5+i) & 7]) {
                    found = false;
                    break;
                }
            }
            if(found == true) {
                printf("Found 'meta' at position %lu\n", index - strlen(target));
                return index;
            }
        }
    }
    printf("found no meta box in all %d positions\n",index);
    return -1; 
}

void print_tree_rec(atom_t* node, int level) {
    int i;
    for(i=0; i<level; i++) {
        printf(".");
    }

    //skip root content, it's not *really* an atom
    if(node->parent != NULL) { 
        printf("%llu %s\n", node->len.word, node->name);
    }
    for(i=0; i<node->children.size(); i++) {
        print_tree_rec(node->children[i], level+1);
    }
}
void print_tree(atom_t* node) {
    print_tree_rec(node, 0);
}

void output_tree(atom_t* node, FILE *out_file) {
    int i;
    
    //skip root content, it's not *really* an atom
    if(node->parent != NULL) {
        for(i = 3; i >= 0; i--) {
            fwrite(&node->len.bytes[i], 1, 1, out_file);
        }
        fwrite(node->name, 4, 1, out_file);
        if(node->data_size > 0 && node->data != NULL) {
            fwrite(node->data, node->data_size, 1, out_file);
        }
    }

    for(i=0; i < node->children.size(); i++) {
        output_tree(node->children[i], out_file);
    }
}

void adjust_stco_offset(atom_t *stco, int offset_adjust) {
    int i,j;
    //Offset past the version byte
    unsigned char* stco_data_ptr = (stco->data + 4);
    int stco_entries = htonl(*((uint32_t*)(stco_data_ptr)));

    //Ofset version bytes and length bytes 
    stco_data_ptr = stco->data + 8;

    //Read the bytes in big-endian order,
    //subtract offset, 
    //write back out.
    for(i = 0; i < stco_entries; i++) {
        uint32_t stco_offset = htonl(*((uint32_t*)(stco_data_ptr)));
        stco_offset -= offset_adjust;
        *((uint32_t*)stco_data_ptr) = htonl(stco_offset);
        stco_data_ptr += 4;
    }
}

atom_t* build_tree(FILE* m4a_file) {

    bool visited_mdat = false;

    //Place to hold the current working atom.
    atom_t *atom;
    
    //Create an abstract root node to hold the top-level
    //atom list.
    atom_t *root = (atom_t*)malloc(sizeof(atom_t));
    root->parent == NULL;
    root->data_remaining = -1;

    atom_t *current_parent = root;

    atom_t *stco = NULL;

    uint64_t chunk_offset_adjust = 0;

    int level = 0;

    /* Loop through the rest of the atoms */
    while((atom = get_next_box(m4a_file))->len.word > 0) {
        int i;
        //Set the parent of the newly created atom
        atom->parent = current_parent; 

        //Note whether or not we've visited the mdat box
        //So that we know if stco offsets must be 
        //adjusted later.
        if(strncmp(atom->name, "mdat", 4) == 0) {
            visited_mdat = true;
        }

        //If it's a meta box, don't add it to the
        //current atom list, so it's removed from 
        //the internal tree structure. Also, iterate
        //back up through the parent pointers adjusting
        //box sizes to account for its removal. 
        if(strncmp(atom->name, "meta", 4) == 0) {
            //reset the parent values
            atom_t *cur = atom->parent;
            while(cur != NULL) {
                cur->len.word -= atom->len.word;
                cur = cur->parent;
            } 
            //update the chunk offset adjustment
            //but only if this meta chunk is before
            //the mdat box. If it's after,
            //it won't change stco offsets relative
            //to the file start.
            if(visited_mdat == false) {
                chunk_offset_adjust += atom->len.word;
            }
        } else {
            current_parent->children.push_back(atom);
        }

        //If we have a chunk_offset_adjust and
        //the current atom is stco, 
        //save it so we can adjust it at the end if necessary.
        if(strncmp(atom->name, "stco", 4) == 0) {
            stco = atom;
        }

        //Subtract size of current atom from
        //the data_remaining of the parent
        //before the next step, which might reset the
        //parent pointer to a new level.
        if(current_parent != root) {
            current_parent->data_remaining -= atom->len.word;
        }

        //If the atom has data_remaining set, then must have some children
        if(atom->data_remaining > 0) {
            current_parent = atom;
            level++;
        }

        //Check if we have data remaining in the parent.
        //If not, if atom was the last one in the parent.
        //So, move back up one level
        //Make sure we haven't overrun any expected sizes in 
        //parents further up
        if(current_parent != root) {
            if(current_parent->data_remaining < 0) {
                printf("Something wrong: child atom overruns the parent size.");
                printf("Parent name is: %s\n",current_parent->name);
                exit(0);
            } 

            //We're done getting the children of this parent, move back up.
            while (current_parent && current_parent->parent && current_parent->data_remaining == 0) {
                current_parent = current_parent->parent;
                level--;
            }
        }
    }
    
    if(stco != NULL) {
        adjust_stco_offset(stco, chunk_offset_adjust);
    }

    return root;
}




int main(int argc, char** argv) {
    
    FILE *m4a_file;
    char test[5] = "ftyp";
    uint32_t *test_convert = (uint32_t*)test;
   
    if(argc < 3) {
        printf("Usage: m4mudex <infilename> <outfilename>");
        exit(1);
    } 

    m4a_file = fopen(argv[1], "rb");
    printf("\nChecking to see if source file has a meta box: \n");
    find_meta(m4a_file);
    rewind(m4a_file); 
    
    printf("\n");

    if (m4a_file == NULL) {
        printf("Provide the name of an existing m4a file to parse\n");
        exit(1);
    } 

    atom_t* m4a_tree = build_tree(m4a_file);

    printf("printing modified tree:\n");
    print_tree(m4a_tree);
    printf("\n");
    
    FILE *out_file;
    out_file = fopen(argv[2], "wb");
    output_tree(m4a_tree, out_file);
    fclose(out_file); 

    printf("\nVerifying that output file has no meta box: \n");
    out_file = fopen(argv[2], "rb");
    find_meta(out_file);
    

}
