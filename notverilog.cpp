   
#include <iostream>
  #include <cstdint>
  #include <vector>
  #include <cstring>
  #define hex uint32_t
  #define sighex int32_t //signed hex (postive or negative)
  //layed out functions and logic for a cpu, should be risc v, 32 bit, a bit of sram cache, and 128mb of psram
  //goal is for tinycore linux to be booted and have a graphical desktop
  //should be made of 7400 series chip, specifically dense ones to achive low cost and high clock speeds like 20mhz(not really high but better than others)
  //globals
  const hex RAM_BASE = 0x80000000;  //where ram starts
  std::vector<uint8_t> ram(1 << 27);     //128 MiB
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

  hex program[] = {
0x00C00093, // addi x1, x0, 12
0x00A0F113, // andi x2, x1, 10
0x00A0E193, // ori  x3, x1, 10
0x00A0C213, // xori x4, x1, 10  
  };

  hex fetch() {

  hex a = pc - RAM_BASE;
       if (a > ram.size() - 4) {printf("pc 0x%08X: processor has exploded\n", pc); exit(1);} //invalid ram error
  return ram[a] | (hex)ram[a+1] << 8 | (hex)ram[a+2] << 16 | (hex)ram[a+3] << 24;
  
  }
  void decode(hex i) {

   opticode = i & 0x7F ;
   rd = (i >> 7) & 0x1F;
   funct3 = (i >> 12) & 0x07;
   rs1 = (i >> 15) & 0x1F;
    rs2 = (i >> 20) & 0x1F;
   imm = (i >> 20) & 0xFFF;
   funct7 = (i >> 25) & 0x7F;
   if ( imm & 0x800 ) {
       imm = imm | 0xFFFFF000;}
       bimm = ((i >> 8 ) & 0xF) << 1; //loop logic extremly overcomplicated because you have to cram everything into a 32 bit number
       bimm = bimm | (((i >> 25) & 0x3F) << 5);
       bimm = bimm | (((i >> 7) & 1) << 11);
       bimm = bimm | (((i >> 31) & 1) << 12);

       if((bimm & 0x1000)!=0) {bimm = bimm | 0xFFFFE000;}
       printf("bimm=0x%X\n", bimm);
       uimm = i & 0xFFFFF000;

       jimm = ((i >> 21) & 0x3FF) << 1; // have to repeat all this for j type too
       jimm = jimm | (((i >> 20) & 1) << 11);
       jimm = jimm | (((i >> 12) & 0xFF) << 12);
       jimm = jimm | (((i >> 31) & 1) << 20);
       if (jimm & 0x100000) { jimm = jimm | 0xFFE00000; }
  }
 
void execute(hex insr) {
switch (opticode) {

  case 0x37: x[rd] = uimm;                break;  //lui
  case 0x17: x[rd] = pc + uimm;           break;  //auipc
  case 0x6F: x[rd] = pc + 4; nextpc = pc + jimm; break;  //jal
  case 0x67: { hex t = (x[rs1] + imm) & ~1u; x[rd] = pc + 4; nextpc = t; } break; //jalr

  case 0x63:                                      //branches
    switch (funct3) {
      case 0: if (x[rs1] == x[rs2]) nextpc = pc + bimm; break; //beq
      case 1: if (x[rs1] != x[rs2]) nextpc = pc + bimm; break; //bne
      case 4: if (sighex(x[rs1]) <  sighex(x[rs2])) nextpc = pc + bimm; break; //blt
      case 5: if (sighex(x[rs1]) >= sighex(x[rs2])) nextpc = pc + bimm; break; //bge
      case 6: if (x[rs1] <  x[rs2]) nextpc = pc + bimm; break; //bltu
      case 7: if (x[rs1] >= x[rs2]) nextpc = pc + bimm; break; //bgeu
      default: goto bad;
    }
  break;
  case 0x13:                                 
    switch (funct3) {
      case 0: x[rd] = x[rs1] + imm; break;  //addi
   case 4: x[rd] = x[rs1] ^ imm; break;  //xori
    case 6: x[rd] = x[rs1] | imm; break;  //ori
     case 7: x[rd] = x[rs1] & imm; break;  //andi
     default: goto bad;                    //slti sltiu slli srli srai
    }
    break;
  case 0x33:                                      //ALU register
    switch (funct3) {
      case 0: x[rd] = (funct7 == 0x20) ? x[rs1] - x[rs2] : x[rs1] + x[rs2]; break; //add/sub
      default: goto bad;                    //sll slt sltu xor srl sra or and
    }
 break;

  case 0x03: goto bad;                            //loads  lb lh lw lbu lhu
case 0x23: goto bad;                            //stores sb sh sw
  case 0x73: goto bad;                            //system ecall ebreak

  default:
  bad:
    printf("unknown instruction 0x%08X at pc 0x%08X , expect processer to detonate\n", insr, pc);
    exit(1);
}
x[0] = 0; //register 0 is always 0
}


  int main() {

  std::memcpy(ram.data(), program, sizeof(program));
  for(;;) { //loop
  nextpc = pc +4;
  hex b = fetch();
      if(b==0) {break;}

  printf("0x%08X\n", b);

  decode(b);
  execute(b);
  pc= nextpc;

  }
   std::cout<<"\ncpu done \n"; //debug
   printf("x1=%d x2=%d x3=%d x4=%d\n", x[1], x[2], x[3], x[4]);
  }
