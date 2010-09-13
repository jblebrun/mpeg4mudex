/***
 * Removes the meta boxes from the provided mpeg4 source file, writing the result
 * out to a new file given the provided filename.
 *
 * This sample program only works for media files that stay within 32-bit box/file sizes.
 * If the file is larger than the 32-bit maximum, the following additions could be made:
 *
 * TODO
 * 1. Check for 64-bit boxes size encoding (atom size=1) and adjust accordingly.
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
 *
 * If we were not stripping the meta box, it may have also been necessary to adjust 
 * values in the 'iloc' and 'dref' sub-boxes of the meta box, not sure.
 */
const char *const containers_of_interest = "moov|udta|trak|mdia|minf|stbl";
typedef struct atom_t {
    atom_t* parent;
    uint32_t len;
    char name[5];
    uint32_t data_size;
    int32_t data_remaining;
    unsigned char* data;
    std::vector<atom_t*> children;
    bool active;
} atom_t;


/***
 * Find the next box (atom) starting from the current
 * position of the provided m4a file. 
 *
 * Allocates memory for the atom if necessary, and returns 
 * a new atom_t with information about the new atom.
 */
atom_t* get_next_box(FILE* m4a_file) {
    atom_t *atom = (atom_t*)malloc(sizeof(atom_t));
   
    /* Read size in big-endian order */
    fread(&atom->len, 4, 1, m4a_file);
    atom->active = true;
    atom->len = htonl(atom->len);
    
    fread(atom->name, 1, 4, m4a_file);
   
    /* If the standard length word is 1, then we
     * expect an 8-byte length immediately follow
     * the name.
     *
     * Also the header is effectively 16 bytes now. */ 
    //if(atom->len == 1) {
        /* Read in big-endian order */
    //    for(i = 7; i >= 0; i--) {
    //        fread(&(atom->len.bytes[i]), 1, 1, m4a_file);
    //    }
    //    atom->data_size = atom->len.word - 16;
    //} else {
        atom->data_size = atom->len - 8;
    //}

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
    uint32_t i;
    for(i = 0; i < strlen(target); i++) {
        tmp <<= 8;
        tmp |= (unsigned char)target[i];
    }
    const unsigned int target_checksum = tmp; //Sum of characters in "meta"
    uint32_t checksum = 0;
    unsigned char c = 0;
    unsigned char buffer[8] = {0,0,0,0,0,0,0,0}; 
    int index = 0;
    while(fread(&c, 1, 1, m4a_file) > 0) {
        index++;
        buffer[index & 7] = c;
        checksum <<= 8;
        checksum |= c;
        if(checksum == target_checksum) {
            printf("Found 'meta' at position %lu\n", index - strlen(target));
            return index;
        }
    }
    printf("found no meta box in all %d positions\n",index);
    return -1; 
}

/* Print out the atom tree representation
 * on stdout
 */
void print_tree_rec(atom_t* node, uint8_t level) {
    uint8_t i;
    for(i=0; i<level; i++) {
        printf(".");
    }
    //skip root content, it's not *really* an atom
    if(node->parent != NULL) { 
        printf("%u %s", node->len, node->name);
        if(strncmp(node->name, "stco", 4) == 0) {
            uint32_t stco_entries = htonl(*((uint32_t*)(node->data + 4)));
            printf(" (%d entries)", stco_entries);
            for(i=0; i <= (10 > stco_entries ? stco_entries : 10); i++) {
                printf(" %d ", htonl(*((uint32_t*)(node->data + 8 + 4*i))));    
            }
            if(stco_entries > 10) {
                printf("...");
            }
            
        }
        printf("\n");
    }
    for(i=0; i<node->children.size(); i++) {
        if(node->children[i]->active == true) {
            print_tree_rec(node->children[i], level+1);
        }
    }
}
void print_tree(atom_t* node) {
    print_tree_rec(node, 0);
}

//Write the atoms back out to file.
void output_tree(atom_t* node, FILE *out_file) {
    uint32_t i;
    
    //skip root content, it's not *really* an atom
    if(node->parent != NULL) {
        uint32_t out_len = htonl(node->len);
        fwrite(&out_len, 4, 1, out_file);
        fwrite(node->name, 4, 1, out_file);
        if(node->data_size > 0 && node->data != NULL) {
            fwrite(node->data, node->data_size, 1, out_file);
        }
    }

    for(i=0; i < node->children.size(); i++) {
        if(node->children[i]->active == true) {
            output_tree(node->children[i], out_file);
        }
    }
}

//Given an atom that we expect to be a stco block,
//and an offset_adjustment, fix the data portion of the 
//atom so that the offsets are reduced by the adjustment
//amount
void adjust_stco_offset(atom_t *stco, int offset_adjust) {
    int i;
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

//Strip meta boxes, returning the size of
//meta tags before mdat boxes for later
//adjustment.
void strip_meta_box_rec(atom_t *node, bool do_accumulate, uint32_t &accumulator, atom_t **stco) {
    uint32_t i;
    if(strncmp(node->name, "mdat", 4) == 0) {
        do_accumulate = false;
    } else if(strncmp(node->name, "stco", 4) == 0) {
        *stco = node;
    } else if(do_accumulate && strncmp(node->name, "meta", 4) == 0) {
        accumulator += node->len;
        node->active = false;
    } 

    for(i = 0; i < node->children.size(); i++) {
        strip_meta_box_rec(node->children[i], do_accumulate, accumulator, stco);
    }
}
void strip_meta_box(atom_t *node) {
    uint32_t offset_adjust = 0;
    atom_t* stco;
    strip_meta_box_rec(node, true, offset_adjust, &stco);
    adjust_stco_offset(stco, offset_adjust);
}


//Create a representation of the tree structure of the atoms
//This function is called recursively. If an atom is marked as a 
//container, move through the data section of the atom sub-atom
//at a time, otherwise just dump the whole data thing into a blob.
atom_t* build_tree(FILE* m4a_file) {

    //Place to hold the current working atom.
    atom_t *atom;
    
    //Create an abstract root node to hold the top-level
    //atom list.
    atom_t *root = (atom_t*)calloc(sizeof(atom_t), 1);

    atom_t *current_parent = root;

    /* Loop through the rest of the atoms */
    while((atom = get_next_box(m4a_file))->len > 0) {
        //Set the parent of the newly created atom
        atom->parent = current_parent; 

        //Add new atom to the current parent list.
        current_parent->children.push_back(atom);

        //Subtract size of current atom from
        //the data_remaining of the parent
        //Note: this must occur before the next step
        //which might change the level. Don't try to
        //include it in the following step.
        if(current_parent != root) {
            current_parent->data_remaining -= atom->len;
        }

        //If the atom has data_remaining set, then must have some children
        if(atom->data_remaining > 0) {
            current_parent = atom;
        }

        //Check if we have data remaining in the parent.
        //If not, if atom was the last one in the parent.
        //So, move back up one level
        if(current_parent != root) {
            //Make sure we haven't overrun any expected sizes in the parent 
            if(current_parent->data_remaining < 0) {
                printf("Something wrong: child atom overruns the parent size.");
                printf("Parent name is: %s\n",current_parent->name);
                exit(0);
            } 

            //We're done getting the children of this parent, move back up.
            while (current_parent && current_parent->parent && current_parent->data_remaining == 0) {
                current_parent = current_parent->parent;
            }
        }
    }
    
    return root;
}

int main(int argc, char** argv) {
    FILE *m4a_file;
    FILE *out_file;
   
    //Check inputs, open file, check for success
    if(argc < 3) {
        printf("Usage: m4mudex <infilename> <outfilename>");
        exit(1);
    } 
    m4a_file = fopen(argv[1], "rb");
    if (m4a_file == NULL) {
        printf("Provide the name of an existing m4a file to parse\n");
        exit(1);
    } 

    //Quick sanity check on input file
    printf("\nChecking to see if source file has a meta box: \n");
    find_meta(m4a_file);
    rewind(m4a_file); 

    //Build the tree
    atom_t* m4a_tree = build_tree(m4a_file);

    //Show the tree
    printf("printing original tree:\n");
    print_tree(m4a_tree);
    printf("\n");
    
    //Get rid of metas and adjust offsets
    strip_meta_box(m4a_tree);

    //Show the modified tree
    printf("printing modifiedtree:\n");
    print_tree(m4a_tree);
    printf("\n");
   
    //Write out the modified tree. 
    out_file = fopen(argv[2], "wb");
    output_tree(m4a_tree, out_file);
    fclose(out_file); 

    //Verify the output file
    printf("\nVerifying that output file has no meta box: \n");
    out_file = fopen(argv[2], "rb");
    find_meta(out_file);
    

}
