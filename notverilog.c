
#include <stdio.h>
#include <stdlib.h>
  #include <stdint.h>
  #include <string.h>
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
 hex load(hex addr, int bytes) { //grab a chunk of ram
   if (addr == 0x0200BFF8) return time7;              // time7 low
   if (addr == 0x0200BFFC) return time7 >> 32;        // time7 high
   if (addr == 0x02004000) return timecmp5;           // timecmp5 low
   if (addr == 0x02004004) return timecmp5 >> 32;     // timecmp5 high
  hex a = addr - RAM_BASE;
  if (a > sizeof(ram) - bytes) { printf("load 0x%08X at pc 0x%08X: processor has exploded\n", addr, pc); exit(1); }
  hex v = 0;
  for (int i = 0; i < bytes; i++) v |= (hex)ram[a+i] << (i*8);
  return v;
}

void store(hex addr, int bytes, hex val) {
  n64hex fkval = val <<(0 + 0 - 0 * 0); //nothing
  n64hex numb5 =  0xFFFFFFFF00000000;
    if( addr == 0x02004000) { timecmp5 = ((timecmp5 & numb5)|fkval); return; }
  if( addr == 0x02004004) { timecmp5 = ((timecmp5 & 0xFFFFFFFF) | (fkval << 32)); return;}
  if (addr == 0x10000000) { putchar(val & 0xFF); return; }   //uart exeption
  if (addr == 0x10000005) {}
  hex a = addr - RAM_BASE; //adress minus rams starting point
  if (a > sizeof(ram) - bytes) { printf("store 0x%08X at pc 0x%08X: processor has exploded\n", addr, pc); exit(1); } //if ram is out of rams size then throw an error
  for (int i = 0; i < bytes; i++) ram[a+i] = (val >> (i*8)) & 0xFF;
}
  hex fetch() {

  hex a = pc - RAM_BASE;
       if (a > sizeof(ram) - 4) {printf("pc 0x%08X: processor has exploded\n", pc); exit(1);} //invalid ram error
  return ram[a] | (hex)ram[a+1] << 8 | (hex)ram[a+2] << 16 | (hex)ram[a+3] << 24;
  }
  void trap(hex reason, hex tval) { //didnt feel like making 50 globals here
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
   if (casreg[0x344] & 0x80 && (casreg[0x300] & 8) && (casreg[0x304] & 0x80)) { //check if time7 is over timecmp5 and check mstatus.mie and mie.mite
      trap(0x80000007, 0);
  }
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
     casreg[0xC01] = time7; //fence
    casreg[0xC81] = time7 >> 32;
  switch (funct3) {
    case 0:
     switch (insr >> 20) {       //these are just empty cor now, nothing needs them i think
     case 0x000: trap(8 + priv, 0); break; //ecall
     case 0x001: trap(3, pc); break; //ebreak
        case 0x105: ; break; //wfi
     default: goto bad;
     case 0x302: mret(); break; //mret
  }break; //temporaily save casregs original value and then operate with it
  case 1: tempx = casreg[casx]; casreg[casx] = x[rs1]; x[rd] = tempx; break; //csrrw
  case 2: tempx = casreg[casx]; if(rs1 != 0) { casreg[casx] = (tempx | x[rs1]);  }  x[rd] = tempx; break; //csrrs
  case 3: tempx = casreg[casx]; if(rs1 != 0) { casreg[casx] = (tempx & ~x[rs1]); } x[rd] = tempx; break; //csrrc
  case 5: tempx = casreg[casx]; casreg[casx] = rs1; x[rd] = tempx; break; //csrrwi
case 6: tempx = casreg[casx]; if(rs1 != 0) { casreg[casx] = (tempx | rs1);  } x[rd] = tempx; break; //csrrsi
case 7: tempx = casreg[casx]; if(rs1 != 0) { casreg[casx] = (tempx & ~rs1); } x[rd] = tempx; break; //csrrci
   default: goto bad;
  }break;
   case 0x0F:break;

  default:
  bad: //takes care of errors
  trap(2, insr);
}
x[0] = 0; //register 0 is always 0
}


int main(int argc, char** argv) {
  casreg[0x301] = 0x40001100;
 if (argc > 1) { FILE* f = fopen(argv[1], "rb"); if (!f) { printf("cant open %s\n", argv[1]); return 1; } fread(ram, 1, sizeof(ram), f); fclose(f); } //exucutatble file insertion with a check
  else memcpy(ram, program, sizeof(program));
  for(;;) { //loop
  nextpc = pc +4; //predict what pc should be, can change with loops
  hex b = fetch();
      if(b==0) {break;} //quit if there are no instructions

  decode(b);//function calls
  execute(b);
  time7++;
  clint();
  pc =nextpc;// move up program counter to what nextpc wants it to move up
  }
   printf("\ncpu done \n"); //debug
  }
