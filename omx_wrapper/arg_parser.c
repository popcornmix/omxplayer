#include <stdio.h>
#include <unistd.h>
#include <getopt.h>

#include "debug.h"

#include "omx_cmd.h"
#include "arg_parser.h"

#define USAGE 
/**
 *
 *
 */
static struct option long_options[] = {
    {"play" , 		required_argument, 0, 0},
    {"pause" , 	    no_argument, 	   0, 0},
    {"stop" , 	    no_argument, 	   0, 0},
    {"playlist" , 	required_argument, 0, 0},
    {"volumeup" , 	no_argument, 	   0, 0},
    {"volumedown", no_argument,       0, 0},
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

        //FIXME: switch should be more robust for '?'
        switch(opt) {//if the argument is not belong to above, getopt() will not extract it
            case 0:/// long option will jump here
                dlog("option %s", long_options[option_index].name);

                if(optarg)
                    dlog(" with arg %s", optarg);
                printf("\n");

                omx_cmd(long_options[option_index].name, optarg);//TODO: implement
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
