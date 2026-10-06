
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <poll.h>
#include <setjmp.h>
#define hex uint32_t //unsigned hex(uses the word hex instaid of uint32_t because its easier to type)
#define sighex int32_t //signed hex (postive or negative)
#define n64hex uint64_t //unsigned 64 bit hex needed for mulh and its buds
#define sign64hex int64_t //signed 64 bit hex for the same reason as unsigned but signed
//layed out functions and logic for a cpu, should be risc v, 32 bit, a bit of sram cache, and 128mb of psram
//goal is for tinycore linux to be booted and have a graphical desktop
//should be made of 7400 series chips, specifically dense ones to have it cost less and get high clock speeds like 20mhz(not really high but better than others)
//globals
const hex RAM_BASE = 0x80000000;  //where ram starts
uint8_t ram[1 << 27];     //128 MiB
hex x[32];  //32 cpu registers
hex pc = RAM_BASE; //program counter
hex nextpc;   // go to the next instruction

//instruction peices
hex opticode; //type of thing being operated
hex rd; //what to add to
hex funct3; //wierd switching
hex rs1; //number to be added into
hex rs2; //rs, but 2
hex imm; //number added into rs1
hex bimm; //branch
hex funct7; //also weird switching
hex uimm;   //u type  imm
hex jimm;  // j type imm
hex simm; //s type imm
hex shiftam; //shift amount
hex casreg[4096]; //control and status register, 12 bit(2^12 = 4096)
hex casx; // magic expression to get csr's value
hex tempx; //temporary holder for csr
hex minen; //interrupt enable bits
hex mpp; //prvious privledge mode
hex mpie; //previose interrupt enable
hex priv = 3; //privlidge of the cpu starting at machine which is 3
n64hex time7 = 0; //needed for time
n64hex timecmp5 = 0xFFFFFFFFFFFFFFFF; //also for time
hex resaddr;
hex resvalid;
hex noti = 0;
hex bgfxglb;
hex ier;
uint8_t rxbyte; //waiting key
hex rxhave; //if a key is waiting or not
hex mask = 0xC0122;
void trap(hex reason, hex tval);
struct termios saved;

hex sigext(hex v, int bits) { //sign extend
  hex m = 1u << (bits - 1);
  return (v ^ m) - m;
}

hex program[] = { //example, not used anymore because i use the file inserts to test
  0x00C00093, // addi x1, x0, 12
  0x00A0F113, // andi x2, x1, 10
  0x00A0E193, // ori  x3, x1, 10
  0x00A0C213, // xori x4, x1, 10
};

void helpfn() {
  if(rxhave == 1) {return;}
  struct pollfd p;
  p.fd = 0;
  p.events = POLLIN;
  int f = poll(&p, 1, 0);
  if(f == 1) {read(0, &rxbyte, 1); rxhave=1;}
}
int csrexists(hex addr) {
  switch (addr) {
    case 0x100: case 0x104: case 0x105: case 0x106: case 0x140: case 0x141: case 0x142: case 0x143: case 0x144: case 0x180:
    case 0x300: case 0x301: case 0x302: case 0x303: case 0x304: case 0x305: case 0x306: case 0x310:
    case 0x340: case 0x341: case 0x342: case 0x343: case 0x344:
    case 0xF11: case 0xF12: case 0xF13: case 0xF14:
    case 0xC00: case 0xC01: case 0xC02: case 0xC80: case 0xC81: case 0xC82:
    case 0xB00: case 0xB02: case 0xB80: case 0xB82:
      return 1;
  }
  return 0;
}
hex csrread(hex addr) {
  if(addr == 0x100) {return casreg[0x300] & mask;}
  if(addr ==  0x104) {return casreg[0x304] & casreg[0x303];}
  if(addr ==  0x144) {return casreg[0x344] & casreg[0x303];}
  if(addr == 0xC00 || addr == 0xC01 || addr == 0xC02 || addr == 0xB00 || addr == 0xB02) {return time7;} //one instruction per tick so cycle, time and instret are the same
  if(addr == 0xC80 || addr == 0xC81 || addr == 0xC82 || addr == 0xB80 || addr == 0xB82) {return time7 >> 32;}
  return  casreg[addr];
}
void csrwrite(hex addr, hex val) {
  if(addr == 0x100) {casreg[0x300] = (casreg[0x300] & ~mask) | (val & mask);}
  else if(addr == 0x104) { casreg[0x304] =(casreg[0x304] & ~casreg[0x303]) | (val & casreg[0x303]);}
  else if(addr == 0x144) { casreg[0x344] =(casreg[0x344] & ~casreg[0x303]) | (val & casreg[0x303]);}
  else(casreg[addr] = val);
}
void sret() {
  nextpc = casreg[0x141];
  priv = (casreg[0x300] >> 8) & 1;
  casreg[0x300] &= ~(1u << 1);
  casreg[0x300] |= ((casreg[0x300] >> 5) & 1) << 1;
  casreg[0x300] |= (1u << 5);
  casreg[0x300] &= ~(1u << 8);
}
hex pteread(hex pa) { //page table entries are 4 bytes in physical ram
  hex a = pa - RAM_BASE;
  if (a > sizeof(ram) - 4) return 0; //outside ram counts as invalid
  return ram[a] | (hex)ram[a+1] << 8 | (hex)ram[a+2] << 16 | (hex)ram[a+3] << 24;
}
void ptewrite(hex pa, hex val) {
  hex a = pa - RAM_BASE;
  for (int i = 0; i < 4; i++) ram[a+i] = (val >> (i*8)) & 0xFF;
}
jmp_buf pfjmp; //jumps back to the main loop so a faulting instruction never finishes
void pagefault(int type, hex vaddr) {
  hex cause = 13; //load
  if(type == 2) {cause = 15;} //store
  if(type == 3) {cause = 12;} //fetch
  trap(cause, vaddr);
  longjmp(pfjmp, 1);
}
hex translate(hex vaddr, int type) { //the most annoying part, the mmu, type 1 load 2 store 3 fetch
  hex vpn1 = vaddr >> 22;
  hex vpn0 = (vaddr >> 12) & 0x3FF;
  hex offset = vaddr & 0xFFF;
  hex p = priv;
  hex pa;
  if(type != 3 && priv == 3 && (casreg[0x300] & (1u << 17))){ p = (casreg[0x300] >> 11) & 3;}
  if (p == 3 || !(casreg[0x180] >> 31)) return vaddr;
  hex a = (casreg[0x180] & 0x3FFFFF) * 4096;
  hex pteaddr = a + vpn1 * 4;
  hex pte = pteread(pteaddr);
  if(!(pte & 1) || ((pte & 4) && !(pte & 2))) {pagefault(type, vaddr);}
  if(pte & 0xA) {
    if((pte >> 10) & 0x3FF) {pagefault(type, vaddr);}
    pa = ((pte >> 20) << 22) | (vaddr & 0x3FFFFF);
  }
  else {
    a = (pte >> 10) * 4096;
    pteaddr = a + vpn0 * 4;
    pte = pteread(pteaddr);
    if(!(pte & 1) || ((pte & 4) && !(pte & 2))) {pagefault(type, vaddr);}
    if(!(pte & 0xA)) {pagefault(type, vaddr);}
    pa = ((pte >> 10) << 12) | offset;
  }
  if(type == 3 && !(pte & 8)) {pagefault(type, vaddr);}
  if(type == 1 && !((pte & 2) || ((pte & 8) && (casreg[0x300] & (1u << 19))))) {pagefault(type, vaddr);}
  if(type == 2 && !(pte & 4)) {pagefault(type, vaddr);}
  if(p == 0 && !(pte & 0x10)) {pagefault(type, vaddr);}
  if(p == 1 && (pte & 0x10) && (type == 3 || !(casreg[0x300] & (1u << 18)))) {pagefault(type, vaddr);}
  hex newpte = pte | 0x40 | (type == 2 ? 0x80 : 0);
  if(newpte != pte) {ptewrite(pteaddr, newpte);}
  return pa;
}
hex load(hex addr, int bytes) { //grab a chunk of ram
  addr = translate(addr, 1);
  if (addr == 0x0200BFF8) return time7;              // time7 low
  if (addr == 0x0200BFFC) return time7 >> 32;        // time7 high
  if (addr == 0x02000000) return (casreg[0x344] >> 3) & 1; //msip, software interrupt
  if (addr == 0x02004000) return timecmp5;           // timecmp5 low
  if (addr == 0x02004004) return timecmp5 >> 32;     // timecmp5 high
  if (addr == 0x10000005) if (addr == 0x10000005) {helpfn(); return 0x60 | rxhave;}//uart ready
  if (addr == 0x10000000) {rxhave = 0; return rxbyte;} //recive register
  if (addr == 0x10000001) return ier; //what inturrupts?
  if (addr == 0x10000002) { helpfn(); if(rxhave == 1 && (ier  & 1) !=0){return 4;} else { return ((ier & 2) != 0) ? 2 : 1;}} //why did it interrupt?
  if (addr == 0x10000004) return 0; //modem contrtol
  if (addr == 0x10000003) return bgfxglb; //settings
  if (addr == 0x10000006) return 0; //modem connected??
  if (addr == 0x10000007) return 0; //no job

  hex a = addr - RAM_BASE;
  if (a > sizeof(ram) - bytes) { printf("load 0x%08X at pc 0x%08X: processor has exploded\n", addr, pc); exit(1); }
  hex v = 0;
  for (int i = 0; i < bytes; i++) v |= (hex)ram[a+i] << (i*8);
  return v;
}

void store(hex addr, int bytes, hex val) {
  addr = translate(addr, 2);
  n64hex fkval = val <<(0 + 0 - 0 * 0); //nothing
  n64hex numb5 =  0xFFFFFFFF00000000;
  if( addr == 0x02000000) { if(val & 1) {casreg[0x344] |= 8;} else {casreg[0x344] &= ~8u;} return; } //msip sets mip.msip
  if( addr == 0x02004000) { timecmp5 = ((timecmp5 & numb5)|fkval); return; }
  if( addr == 0x02004004) { timecmp5 = ((timecmp5 & 0xFFFFFFFF) | (fkval << 32)); return;}
  if (addr == 0x10000000) { if ((bgfxglb & 0x80) == 0) {putchar(val & 0xFF); } return; }   //uart exeption
  if (addr == 0x10000001) { if ((bgfxglb & 0x80) == 0) ier = val; return; }
  if (addr == 0x10000002) {  return; }
  if (addr == 0x10000003) { bgfxglb = val; return; }
  if (addr == 0x10000004) {  return; }
  if (addr == 0x10000005) {  return; }
  if (addr == 0x10000006) {  return; }
  if (addr == 0x10000007) {  return; }
  hex a = addr - RAM_BASE; //adress minus rams starting point
  if (a > sizeof(ram) - bytes) { printf("store 0x%08X at pc 0x%08X: processor has exploded\n", addr, pc); exit(1); } //if ram is out of rams size then throw an error
  for (int i = 0; i < bytes; i++) ram[a+i] = (val >> (i*8)) & 0xFF;
}
hex fetch() {
  hex a = translate(pc, 3) - RAM_BASE;
  if (a > sizeof(ram) - 4) {printf("pc 0x%08X: processor has exploded\n", pc); exit(1);} //invalid ram error
  return ram[a] | (hex)ram[a+1] << 8 | (hex)ram[a+2] << 16 | (hex)ram[a+3] << 24;
}
void trap(hex reason, hex tval) { //didnt feel like making 50 globals here
  hex cause = reason & 0x7FFFFFFF;
  hex deleg = (reason >> 31) ? casreg[0x303] : casreg[0x302];
  if (priv <= 1 && ((deleg >> cause) & 1)) {
    casreg[0x141] = pc;
    casreg[0x142] = reason;
    casreg[0x143] = tval;
    hex sie = (casreg[0x300] >> 1) & 1;
    casreg[0x300] &= ~(1u << 5);
    casreg[0x300] |= sie << 5;
    casreg[0x300] &= ~(1u << 1);
     casreg[0x300] &= ~(1u << 8);
    casreg[0x300] |= priv << 8;
     nextpc = casreg[0x105] & ~3u;
     priv =1;
     resvalid = 0;
     noti = 1;
     return;
  }
  if(casreg[0x305] == 0) {printf("trap failed because %u\n", reason); exit(1);}
  casreg[0x341] = pc;
  casreg[0x342] = reason;
  nextpc = casreg[0x305] & ~3u;
  casreg[0x343] =tval;
  hex mie = (casreg[0x300] >> 3) & 1;
  casreg[0x300] &= ~(1u << 7);    // clear mpie
  casreg[0x300] |= mie << 7;      // save mie into it
  casreg[0x300] &= ~(1u << 3);
  casreg[0x300] &= ~(3u << 11); //clear mpp
  casreg[0x300] |= priv << 11; //save priv into it
  resvalid = 0;
  priv = 3;
  noti = 1;
}
void mret() { //same with here just opposite of a trap
  nextpc = casreg[0x341]; //nextpc = mpc
  priv = (casreg[0x300] >>11 ) & 3; //priv = mpp
  casreg[0x300] &= ~(1u << 3);                        // clear mie
  casreg[0x300] |= ((casreg[0x300] >> 7) & 1) << 3;   // mie = mpie
  casreg[0x300] |= (1u << 7);//mpie =1
  (casreg[0x300] &= ~(3u << 11 ));
}
void clint() { //clint does timer interrupts
  if(time7 >= timecmp5) {casreg[0x344] |= 0x80;}
  else{ casreg[0x344] &= ~0x80;}
  if (casreg[0x344] & 0x80 && (((casreg[0x300] & 8) || priv < 3)) && (casreg[0x304] & 0x80)) { //check if time7 is over timecmp5 and check mstatus.mie and mie.mite
    trap(0x80000007, 0);
  }
    else if((casreg[0x344] & 0x20 )&& (casreg[0x304] & 0x20 ) && (casreg[0x303] & 0x20 )&& (priv == 0 || (priv == 1 && (casreg[0x300] & 2)))) {trap(0x80000005, 0);}
}
void decode(hex i) {
  //example - ex = (i >> (starting bit #) ) & 0x(size of number);
  opticode = i & 0x7F ;
  rd = (i >> 7) & 0x1F;
  funct3 = (i >> 12) & 0x07;
  rs1 = (i >> 15) & 0x1F;
  rs2 = (i >> 20) & 0x1F;
  imm = (i >> 20) & 0xFFF;
  minen = ( casreg[0x300] >> 3 ) & 0x1;
  shiftam = (i >> 20) & 0x1F;
  simm = ((i >> 7) & 0x1F) | (((i >> 25) & 0x7F) << 5);   //s type imm
  if (simm & 0x800) { simm = simm | 0xFFFFF000; }
  funct7 = (i >> 25) & 0x7F;
  if ( imm & 0x800 ) {
    imm = imm | 0xFFFFF000;}
    bimm = ((i >> 8 ) & 0xF) << 1; //loop logic extremly overcomplicated because you have to cram everything into a 32 bit number
    bimm = bimm | (((i >> 25) & 0x3F) << 5);
    bimm = bimm | (((i >> 7) & 1) << 11);
    bimm = bimm | (((i >> 31) & 1) << 12);

    if((bimm & 0x1000)!=0) {bimm = bimm | 0xFFFFE000;}
    uimm = i & 0xFFFFF000;

    jimm = ((i >> 21) & 0x3FF) << 1; // have to repeat all this for j type too
    jimm = jimm | (((i >> 20) & 1) << 11);
    jimm = jimm | (((i >> 12) & 0xFF) << 12);
    jimm = jimm | (((i >> 31) & 1) << 20);
    if (jimm & 0x100000) { jimm = jimm | 0xFFE00000; }
}

void execute(hex insr) {
  casx = (insr >> 20) & 0xFFF;  //csr
  switch (opticode) { //switch should avoid haveing to run through 300 ifs and should be faster and less messy

    case 0x37: x[rd] = uimm;                break;  //lui
    case 0x17: x[rd] = pc + uimm;           break;  //auipc
    case 0x6F: x[rd] = pc + 4; nextpc = pc + jimm; break;  //jal
    case 0x67: { hex t = (x[rs1] + imm) & ~1u; x[rd] = pc + 4; nextpc = t; } break; //jalr (the ~ means invert the number)

    case 0x63:                                      //branches
      switch (funct3) {
        case 0: if (x[rs1] == x[rs2]) nextpc = pc + bimm; break; //beq
        case 1: if (x[rs1] != x[rs2]) nextpc = pc + bimm; break; //bne
        case 4: if ((sighex)x[rs1] <  (sighex)x[rs2]) nextpc = pc + bimm; break; //blt
        case 5: if ((sighex)x[rs1] >= (sighex)x[rs2]) nextpc = pc + bimm; break; //bge
        case 6: if (x[rs1] <  x[rs2]) nextpc = pc + bimm; break; //bltu
        case 7: if (x[rs1] >= x[rs2]) nextpc = pc + bimm; break; //bgeu
        default: goto bad; //sends stuff to bad where it throws an error
      }
      break;
        case 0x13:                  //reading of the riscv datasheet so i cant make everything in order
          switch (funct3) {
            case 0: x[rd] = x[rs1] + imm; break;  //addi (add to immediate like addi x1, x0, 11)
            case 4: x[rd] = x[rs1] ^ imm; break;  //xori  (xor the bits together)
            case 6: x[rd] = x[rs1] | imm; break;  //ori (just or the bits together)
            case 7: x[rd] = x[rs1] & imm; break;  //andi (and the bits together)
            case 2: x[rd] = (sighex)x[rs1] < (sighex)imm ? 1 : 0; break; //slti (? basically is (if this)?(then this):(if not then this);)
            case 3: x[rd] = (hex)x[rs1] < (hex)imm ? 1 : 0; break;//sltiu (just sltiu but unsigned)
            case 1:x[rd] = x[rs1] << shiftam; break; //slli (shifting)
            case 5:x[rd] = funct7 == 0 ? (hex)x[rs1] >> shiftam : (sighex)x[rs1] >> shiftam; break; //srli and srai
            default: goto bad;
          }
          break;
            case 0x33:                    //datasheet was in order here
              switch (funct3) { //everything here just compares/ operates with registers
                case 0: switch(funct7) { //layering more switches for mul and div
                case 0: x[rd] = x[rs1] + x[rs2] ; break; //add
                case 1: x[rd] = x[rs1] * x[rs2]; break; //mul
                case 0x20: x[rd] = x[rs1] - x[rs2];break; //sub
                default: goto bad;
                }
                break;     //use 64 bits to shift up for mulh mullsu and mulhu
                case 1: x[rd] = funct7 == 0 ? x[rs1] << (x[rs2] & 31) :  (sign64hex)(sighex)x[rs1] * (sign64hex)(sighex)x[rs2]  >> 32; break; //sll and mulh
                case 2: x[rd] = funct7 == 0 ? (sighex)x[rs1] < (sighex)x[rs2] ? 1:0 : (sign64hex)(sighex)x[rs1] * (n64hex)(hex)x[rs2]  >> 32; break; //slt and mulhsu
                case 3: x[rd] = funct7 == 0 ? (hex)x[rs1] < (hex)x[rs2] ? 1:+0 : (n64hex)(hex)x[rs1] * (n64hex)(hex)x[rs2] >>32; break; // sltu (slt but unsigned ) and mulhu
                case 4: if(funct7 == 0) { x[rd] = x[rs1] ^ x[rs2] ;} //xor
                else {  // div
                  if (x[rs2] == 0) x[rd] = 0xFFFFFFFF;
                  else if (x[rs1] == 0x80000000 && x[rs2] == 0xFFFFFFFF) x[rd] = 0x80000000;
                  else x[rd] = (sighex)x[rs1] / (sighex)x[rs2]; } break;
                case 5: switch(funct7) {
                case 0: x[rd] = (hex)x[rs1] >> (hex)(x[rs2] & 31) ; break; //srl
                case 1:if (x[rs2] == 0) {x[rd] = 0xFFFFFFFF;} else x[rd] = (hex)x[rs1] / (hex)x[rs2]; break; //divu
                case 0x20: x[rd] = (sighex)x[rs1] >> (sighex)(x[rs2] & 31); break; //sra
                default: goto bad;
                } break;
                case 6:if( funct7 == 0 ) { x[rd] = x[rs1] | x[rs2] ;}  //extreml dumb layering becuase divide by 0 is so imprtant
                else {if(x[rs2] == 0) {x[rd] = x[rs1];}
                else if (x[rs1] == 0x80000000 && x[rs2] == 0xFFFFFFFF) {x[rd] = 0;}
                else {x[rd] = (sighex)x[rs1] % (sighex)x[rs2];}} break; //or and rem
                case 7: if(funct7 == 0) { x[rd] = x[rs1] & x[rs2];}
                else if(x[rs2] == 0) {x[rd] = x[rs1];}
                else  {x[rd] = (hex)x[rs1] % (hex)x[rs2];} break; //and and remu
                default: goto bad;
              }break;
                case 0x2F: {    //atomics
                  if((insr >> 27) != 0x02) {translate(x[rs1], 2);} //amos fault as stores
                  hex old = load(x[rs1],4);
                  switch (insr >> 27) {
                    case 0x02:  x[rd] = old; resaddr = x[rs1]; resvalid =1; ;break; //lr.w
                    case 0x03:  if (resvalid && resaddr == x[rs1]) {store(x[rs1], 4, x[rs2]); x[rd] = 0;} else {x[rd] = 1;} resvalid = 0; ;break; //sc.w
                    case 0x01:  store(x[rs1], 4, x[rs2]);
                    x[rd] = old; ;break; //amoswap.w
                    case 0x00: store(x[rs1], 4, old + x[rs2]);
                    x[rd] = old;break; //amoadd.w
                    case 0x04:  store(x[rs1], 4, old ^ x[rs2]);
                    x[rd] = old ;break; //amoxor.w
                    case 0x0C: store(x[rs1], 4, old & x[rs2]);
                    x[rd] = old  ;break; //amoand.w
                    case 0x08:  store(x[rs1], 4, old | x[rs2]);
                    x[rd] = old  ;break; //amoor.w
                    case 0x10:  store(x[rs1], 4, (sighex)old < (sighex)x[rs2] ? old : x[rs2]);
                    x[rd] = old  ;break; //amomin.w
                    case 0x14: store(x[rs1], 4, (sighex)old > (sighex)x[rs2] ? old : x[rs2]);
                    x[rd] = old ;break; //amomax.w
                    case 0x18: store(x[rs1], 4, (hex)old < (hex)x[rs2] ? old : x[rs2]);
                    x[rd] = old  ;break; //amoinu.w
                    case 0x1C: store(x[rs1], 4, (hex)old > (hex)x[rs2] ? old : x[rs2]);
                    x[rd] = old  ;break; //amomaxu.w
                    default: goto bad;
                  }
                }break;
                    case 0x23: {                                    //storing stuff into ram
                      hex addr = x[rs1] + simm;
                      switch (funct3) {
                        case 0: store(addr,1,x[rs2]); break;  //sb (pretty simple, all the stuff here just calls a function)
                        case 1: store(addr,2,x[rs2]); break;  //sh
                        case 2: store(addr,4,x[rs2]); break;  //sw
                        default: goto bad;
                      }
                    } break;
                        case 0x03: {                                    //loads stuff into ram
                          hex addr = x[rs1] + imm;
                          switch (funct3) {
                            case 0: x[rd] = sigext(load(addr,1), 8);  break;  //lb
                            case 1: x[rd] = sigext(load(addr,2), 16); break;  //lh
                            case 2: x[rd] = load(addr,4);             break;  //lw
                            case 4: x[rd] = load(addr,1);             break;  //lbu (no sigext = zero extend)
                            case 5: x[rd] = load(addr,2);             break;  //lhu
                            default: goto bad;
                          }
                        } break;
                            case 0x73:
                              switch (funct3) {
                                case 0:
                                  if(funct7 == 0x09) break; //sfence.vma, no tlb to flush
                                  switch (insr >> 20) {       //these are just empty cor now, nothing needs them i think
                                    case 0x000: trap(8 + priv, 0); break; //ecall
                                    case 0x001: trap(3, pc); break; //ebreak
                                    case 0x105: ; break; //wfi
                                    case 0x102: sret(); break;
                                    default: goto bad;
                                    case 0x302: mret(); break; //mret
                                  }break; //temporaily save casregs original value and then operate with it
                                    case 1: if(!csrexists(casx)) goto bad; tempx = csrread(casx); csrwrite(casx, x[rs1]); x[rd] = tempx; break; //csrrw
                                    case 2: if(!csrexists(casx)) goto bad; tempx = csrread(casx); if(rs1 != 0) { csrwrite(casx, (tempx | x[rs1]));  }  x[rd] = tempx; break; //csrrs
                                    case 3: if(!csrexists(casx)) goto bad; tempx = csrread(casx); if(rs1 != 0) { csrwrite(casx, (tempx & ~x[rs1])); } x[rd] = tempx; break; //csrrc
                                    case 5: if(!csrexists(casx)) goto bad; tempx = csrread(casx); csrwrite(casx, rs1); x[rd] = tempx; break; //csrrwi
                                    case 6: if(!csrexists(casx)) goto bad; tempx = csrread(casx); if(rs1 != 0) { csrwrite(casx, (tempx | rs1));  } x[rd] = tempx; break; //csrrsi
                                    case 7: if(!csrexists(casx)) goto bad; tempx = csrread(casx); if(rs1 != 0) { csrwrite(casx, (tempx & ~rs1)); } x[rd] = tempx; break; //csrrci
                                    default: goto bad;
                              }break;
                                    case 0x0F:break;

                                    default:
                                      bad: //takes care of errors
                                      trap(2, insr);
  }
  x[0] = 0; //register 0 is always 0
}
void restore() { tcsetattr(0, TCSANOW, &saved); }

int main(int argc, char** argv) {
  casreg[0x301] = 0x40141101;
  tcgetattr(0, &saved);
  struct termios raw = saved;
  raw.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(0, TCSANOW, &raw);
   atexit(restore);
   setvbuf(stdout, NULL, _IONBF, 0);
  if (argc > 1) { FILE* f = fopen(argv[1], "rb"); if (!f) { printf("cant open %s\n", argv[1]); return 1; } fread(ram, 1, sizeof(ram), f); fclose(f); } //exucutatble file insertion with a check
  else memcpy(ram, program, sizeof(program));
  for(;;) { //loop
    nextpc = pc +4; //predict what pc should be, can change with loops
    clint(); //check for traps
    if(noti == 0) {
      if(setjmp(pfjmp) == 0) { //a page fault lands back here with 1 and skips the rest
        hex b = fetch(); //fetch
        decode(b);//function calls
        execute(b);
      }
    }
    time7++;
    pc =nextpc;// move up program counter to what nextpc wants it to move up
    noti = 0;
  }
  printf("\ncpu done \n"); //debug
}
