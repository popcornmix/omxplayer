#include <stdio.h>
#include <unistd.h>
#include <getopt.h>

#include "debug.h"

#include "arg_parser.h"

#define USAGE 
/**
 *
 *
 */
static struct option long_options[] = {
    {"Play" , 		required_argument, 0, 0},
    {"Pause" , 	    no_argument, 	   0, 0},
    {"Stop" , 	    no_argument, 	   0, 0},
    {"Playlist" , 	required_argument, 0, 0},
    {"Volume_Up" , 	no_argument, 	   0, 0},
    {"Volume_Down", no_argument,       0, 0},
    {0,						0, 0, 0}
};

void opt_parser(int argc, char* argv[])
{
    int opt;//, optind;

    while(1) {
        //int this_option_optind = optind ? optind: 1;
        int option_index = 0;

        ///< It can input --Play --Pause ...
        opt = getopt_long( argc, argv, "", long_options, &option_index);

        if( opt == -1) {//< no more argument
            dlog("No argument!\n");
            break;
        }

        switch(opt) {
            case 0:/// long option will jump here
                dlog("option %s", long_options[option_index].name);

                if(optarg)
                    dlog(" with arg %s", optarg);
                dlog("\n");
                break;
        }
    }

    if(optind < argc) {
        printf("non-option ARGV-elements: ");
        while( optind < argc)
            printf("%s ", argv[ optind++]);
        printf("\n");
    }

    return;
}
