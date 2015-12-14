//
//  main.c
//  ScreenshotCapture
//
//  Created by Elviss Strazdins on 14/12/15.
//  Copyright © 2015 Elviss Strazdins. All rights reserved.
//

#include <stdio.h>

int main(int argc, const char * argv[])
{
    if (argc < 2)
    {
        printf("Too few arguments\n");
        return 1;
    }
    
    printf("Path: %s\n", argv[1]);
    
    return 0;
}
