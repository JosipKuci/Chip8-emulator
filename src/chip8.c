#include "chip8.h"
#include <memory.h>
#include <stdio.h>
#include "SDL2/SDL.h"
#include <assert.h>
#include <stdlib.h>
#include <time.h>
const char chip8_default_char_set[]=
{
    0xf0,0x90,0x90,0x90,0xf0, //0
    0x20,0x60,0x20,0x20,0x70, //1
    0xf0,0x10,0xf0,0x80,0xf0, //2
    0xf0,0x10,0xf0,0x10,0xf0, //3
    0x90,0x90,0xf0,0x10,0x10, //4
    0xf0,0x80,0xf0,0x10,0xf0, //5
    0xf0,0x80,0xf0,0x90,0xf0, //6
    0xf0,0x10,0x20,0x40,0x40, //7
    0xf0,0x90,0xf0,0x90,0xf0, //8
    0xf0,0x90,0xf0,0x10,0xf0, //9
    0xf0,0x90,0xf0,0x90,0x90, //A
    0xe0,0x90,0xe0,0x90,0xe0, //B
    0xf0,0x80,0x80,0x80,0xf0, //C
    0xe0,0x90,0x90,0x90,0xe0, //D
    0xf0,0x80,0xf0,0x80,0xf0, //E
    0xf0,0x80,0xf0,0x80,0x80, //F 
};

void chip8_init(struct chip8* chip8)
{
    memset(chip8, 0, sizeof(struct chip8));
    memcpy(&chip8->memory.memory,chip8_default_char_set,sizeof(chip8_default_char_set));
    for(int x=0;x<CHIP8_WIDTH;x++)
        {
            for(int y=0;y<CHIP8_HEIGHT;y++)
            {
                chip8->screen.pixels[y][x]=false;
            }
        }
}

void chip8_load(struct chip8* chip8, const char* buffer, size_t size)
{
    assert((size+CHIP8_PROGRAM_LOAD_ADDRESS) < CHIP8_MEMORY_SIZE);
    memcpy(&chip8->memory.memory[CHIP8_PROGRAM_LOAD_ADDRESS], buffer, size);
    chip8->registers.PC = CHIP8_PROGRAM_LOAD_ADDRESS;
}
static void chip8_exec_extended_V_register_operations(struct chip8* chip8, unsigned short opcode)
{
    unsigned char x=(opcode >> 8)&0x000f;
    unsigned char y=(opcode >> 4)&0x000f;
    unsigned char z=opcode & 0x000f; //The last 4 bits of the opcode, used for differentiating between different opcodes
    unsigned short temp = 0;
    switch(z)
    {
        //Sets value of register V[x] to the value of register V[y]
        case 0x00:
            chip8->registers.V[x]=chip8->registers.V[y];
        break;
        //Sets value of register V[x] to the value of OR operation between V[x] and V[y]
        case 0x01:
            chip8->registers.V[x]=chip8->registers.V[x]|chip8->registers.V[y];
        break;
        //Sets value of register V[x] to the value of AND operation between V[x] and V[y]
        case 0x02:
            chip8->registers.V[x]=chip8->registers.V[x]&chip8->registers.V[y];
        break;
        //Sets value of register V[x] to the value of XOR operation between V[x] and V[y]
        case 0x03:
            chip8->registers.V[x]=chip8->registers.V[x]^chip8->registers.V[y];
        break;
        //Sets V[x]=V[x]+V[y], if the result is greater than 2 bits, v[0x0f] is set to 1
        case 0x04:
            temp=chip8->registers.V[x]+chip8->registers.V[y];
            chip8->registers.V[0x0f]=0;
            if(temp>0xff)
            {
                chip8->registers.V[0x0f]=1;
            }
            chip8->registers.V[x]=temp;
        break;
        //sets V[x]=V[x]-V[y], sets V[F] not borrow
        case 0x05:
            chip8->registers.V[0x0f]=0;
            if(chip8->registers.V[x]>chip8->registers.V[y])
            {
                chip8->registers.V[0x0f]=1;
            }
            chip8->registers.V[x]-=chip8->registers.V[y];
        break;
        //If the least-significant bit of V[x] is 1, then V[F] is set to 1, otherwise 0. Then V[x] is divided by 2.
        case 0x06:
            chip8->registers.V[0x0f]=chip8->registers.V[x]&0x01;
            chip8->registers.V[x]/=2;
        break;
        //If V[y] > V[x], then V[F] is set to 1, otherwise 0. Then V[x] is subtracted from V[y], and the results stored in V[x].
        case 0x07:
            chip8->registers.V[0x0f]=chip8->registers.V[y]>chip8->registers.V[x];
            chip8->registers.V[x]=chip8->registers.V[y]-chip8->registers.V[x];
        break;
        //If the most-significant bit of V[x] is 1, then V[F] is set to 1, otherwise to 0. Then V[x] is multiplied by 2.
        case 0x0E:
            chip8->registers.V[0x0f]=chip8->registers.V[x]&0b10000000;
            chip8->registers.V[x]*=2;
        break;
    }
}
static char chip8_wait_for_key_press(struct chip8* chip8)
{
    SDL_Event event;
    while(SDL_WaitEvent(&event))
    {
        if(event.type != SDL_KEYDOWN)
            continue;
        char c = event.key.keysym.sym;
        char chip8_key = chip8_keyboard_map(&chip8->keyboard,c);
        if(chip8_key!=-1)
        {
            return chip8_key;
        }
    }
    return -1;
}


static void chip8_exec_extended_F(struct chip8* chip8, unsigned short opcode)
{
    unsigned char x=(opcode >> 8)&0x000f;   
    switch (opcode & 0x00ff)
    {
        //Set Vx = delay timer value.
        case 0x07:
            chip8->registers.V[x]=chip8->registers.delay_timer;
        break;
        //Wait for a key press, store the value of the key in Vx
        case 0x0A:
            chip8->registers.V[x]=chip8_wait_for_key_press(chip8);
        break;
        //Set delay timer = Vx
        case 0x15:
            chip8->registers.delay_timer=chip8->registers.V[x];
        break;
        //Set sound timer = Vx
        case 0x18:
            chip8->registers.sound_timer=chip8->registers.V[x];
        break;
        //Set I = I + Vx.
        case 0x1E:
            chip8->registers.I+=chip8->registers.V[x];
        break;
        //Set I = location of sprite for digit Vx.
        case 0x29:
            chip8->registers.I=chip8->registers.V[x]*CHIP8_DEFAULT_SPRITE_HEIGHT;
        break;
        //Store BCD representation of Vx in memory locations I, I+1, and I+2
        case 0x33:
        {
            unsigned char hundreds=chip8->registers.V[x]/100;
            unsigned char tens=(chip8->registers.V[x]/10)%10;
            unsigned char ones=chip8->registers.V[x]%10;
            chip8_memory_set(&chip8->memory,chip8->registers.I,hundreds);
            chip8_memory_set(&chip8->memory,chip8->registers.I+1,tens);
            chip8_memory_set(&chip8->memory,chip8->registers.I+2,ones);
        }
        break;
        //Store registers V0 through Vx in memory starting at location I.
        case 0x55:
        {
            for(int i=0;i<=x;i++)
            {
                chip8_memory_set(&chip8->memory,chip8->registers.I+i,chip8->registers.V[i]);
            }
        }
        break;

        case 0x65:
        {
            for(int i=0;i<=x;i++)
            {
                chip8->registers.V[i]=chip8_memory_get(&chip8->memory,chip8->registers.I+i);
            }
        }
        break;
    }
}

static void chip8_exec_extended(struct chip8* chip8, unsigned short opcode)  
{
    unsigned short nnn=opcode & 0x0fff;
    unsigned char x=(opcode >> 8)&0x000f;
    unsigned char y=(opcode >> 4)&0x000f;
    unsigned char kk=opcode&0x00ff;
    unsigned char n = opcode&0x000f;
    switch(opcode & 0xf000)
    {   
        //Jump address, jump to nnn
        case 0x1000:
            chip8->registers.PC=nnn;
        break;
        //Call subroutine at location nnn
        case 0x2000:
            chip8_stack_push(chip8, chip8->registers.PC);
            chip8->registers.PC=nnn;
        break;
        //Skip instruction if V[x]==kk
        case 0x3000:
        {
            if(chip8->registers.V[x]==kk)
            {
                chip8->registers.PC+=2;
            }
            break;
        }
         //Skip instruction if V[x]!=kk
        case 0x4000:
        {
            if(chip8->registers.V[x]!=kk)
            {
                chip8->registers.PC+=2;
            }
            break;
        }
        //5xy0 - Skip instruction if register V[x]==V[y]
        case 0x5000:
        {
            if(chip8->registers.V[x]==chip8->registers.V[y])
            {
                chip8->registers.PC+=2;
            }
            break;
        }
        //sets value of register V[x] to value kk
        case 0x6000:
            chip8->registers.V[x]=kk;
        break;

        //adds to register V[x] the value kk
        case 0x7000:
            chip8->registers.V[x]+=kk;
        break;
        //Defines a whole family of opcodes around the V registers
        case 0x8000:
            chip8_exec_extended_V_register_operations(chip8,opcode);
        break;
        //Skip next instruction if Vx != Vy.
        case 0x9000:
            if(chip8->registers.V[x]!=chip8->registers.V[y])
            {
                chip8->registers.PC+=2;
            }
        break;
        //The value of register I is set to nnn.
        case 0xA000:
            chip8->registers.I=nnn;
        break;
        //The program counter is set to nnn plus the value of V0.
        case 0xB000:
            chip8->registers.PC=nnn+chip8->registers.V[0];
        break;
        //The interpreter generates a random number from 0 to 255, which is then ANDed with the value kk. The results are stored in V[x].
        case 0xC000:
            srand(clock());
            chip8->registers.V[x]=(rand()%255)&kk;
        break;
        //Display n-byte sprite starting at memory location I at (Vx, Vy), set VF = collision.
        case 0xD000:
        {
            const char* sprite=(const char*) &chip8->memory.memory[chip8->registers.I];
            chip8->registers.V[0x0f]=chip8_screen_draw_sprite(&chip8->screen,chip8->registers.V[x],chip8->registers.V[y],sprite,n);
        }
        break;
        //Skip next instruction if key with the value of Vx is pressed
        case 0xE000:
        {
            switch(opcode&0x00ff)
            {
                //Skip next instruction if key with the value of Vx is pressed
                case 0x9e:
                    if(chip8_keyboard_is_pressed(&chip8->keyboard,chip8->registers.V[x]))
                    {
                        chip8->registers.PC+=2;
                    }
                break;
                //Skip next instruction if key with the value of Vx is NOT pressed
                case 0xA1:
                    if(!chip8_keyboard_is_pressed(&chip8->keyboard,chip8->registers.V[x]))
                    {
                        chip8->registers.PC+=2;
                    }
                break;
            }
        }
        break;

        case 0xF000:
            chip8_exec_extended_F(chip8, opcode);
        break;
    }
}


void chip8_exec(struct chip8* chip8, unsigned short opcode)
{
    switch(opcode)
    {
        //Clear the screen
        case 0x00E0:
            chip8_screen_clear(&chip8->screen);
        break;
        //Return from a subroutine
        case 0x00EE:
            chip8->registers.PC=chip8_stack_pop(chip8);
        break;

        default:
            chip8_exec_extended(chip8,opcode);
    }
}
