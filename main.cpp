// Minimal RV32I simulator for RISC-V Simulator 2022
// Parses .data hex format from stdin, executes RV32I, halts on ECALL/EBREAK, prints x10 (a0)
#include <bits/stdc++.h>
using namespace std;

static inline uint32_t sext(uint32_t val, int bits) {
    uint32_t m = 1u << (bits - 1);
    return (val ^ m) - m;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    // Read entire stdin as text
    string s;
    {
        ostringstream oss;
        oss << cin.rdbuf();
        s = oss.str();
    }

    // Parse .data format: lines like "@<addr>" followed by byte tokens (2 hex chars per byte)
    vector<pair<uint32_t, uint8_t>> bytes; // (addr, byte)
    uint64_t cur = 0;
    bool has_cur = false;
    size_t i = 0, n = s.size();
    auto ishex = [](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');};
    auto hexval = [](char c)->int{ if(c>='0'&&c<='9') return c-'0'; if(c>='a'&&c<='f') return 10+c-'a'; return 10+c-'A'; };
    auto read_hex = [&](int maxlen)->pair<bool,uint64_t>{
        uint64_t v=0; int cnt=0; size_t j=i;
        while(j<n && ishex(s[j]) && cnt<maxlen){ v=(v<<4)|hexval(s[j]); j++; cnt++; }
        if(cnt==0) return {false,0};
        i=j; return {true,v};
    };

    while(i<n){
        char c = s[i];
        if(c=='@'){
            i++;
            auto p = read_hex(16);
            if(!p.first) { ++i; continue; }
            cur = p.second;
            has_cur = true;
            continue;
        }
        if(ishex(c)){
            auto p = read_hex(2);
            if(p.first && has_cur){
                uint8_t b = (uint8_t)p.second;
                bytes.emplace_back((uint32_t)cur, b);
                cur += 1;
            }
            continue;
        }
        ++i;
    }

    if(bytes.empty()){
        // No data: nothing to run
        cout << 0 << '\n';
        return 0;
    }

    // Determine memory size
    uint32_t min_addr = UINT32_MAX, max_addr = 0;
    for(auto &p: bytes){ min_addr = min(min_addr, p.first); max_addr = max(max_addr, p.first); }
    uint64_t mem_size64 = (uint64_t)max_addr + 1;
    // add some headroom
    if(mem_size64 < (1ull<<24)) mem_size64 = (1ull<<24); // 16MB
    if(mem_size64 > (1ull<<28)) mem_size64 = (1ull<<28); // cap 256MB
    size_t MEM_SIZE = (size_t)mem_size64;
    vector<uint8_t> mem(MEM_SIZE, 0);
    for(auto &p: bytes){
        uint32_t a = p.first;
        uint8_t b = p.second;
        if((size_t)a < mem.size()) mem[a] = b;
    }

    auto r8 = [&](uint32_t addr)->uint8_t{
        if(addr >= mem.size()) return 0; return mem[addr];
    };
    auto r16 = [&](uint32_t addr)->uint16_t{
        uint32_t v = r8(addr) | (r8(addr+1)<<8);
        return (uint16_t)v;
    };
    auto r32 = [&](uint32_t addr)->uint32_t{
        uint32_t v = r8(addr) | (r8(addr+1)<<8) | (r8(addr+2)<<16) | (r8(addr+3)<<24);
        return v;
    };
    auto w8 = [&](uint32_t addr, uint8_t val){ if(addr < mem.size()) mem[addr]=val; };
    auto w16 = [&](uint32_t addr, uint16_t val){ w8(addr,(uint8_t)(val&0xFF)); w8(addr+1,(uint8_t)((val>>8)&0xFF)); };
    auto w32 = [&](uint32_t addr, uint32_t val){ w8(addr,(uint8_t)(val&0xFF)); w8(addr+1,(uint8_t)((val>>8)&0xFF)); w8(addr+2,(uint8_t)((val>>16)&0xFF)); w8(addr+3,(uint8_t)((val>>24)&0xFF)); };

    uint32_t x[32] = {0};
    uint32_t pc = min_addr == UINT32_MAX ? 0 : 0; // default start at 0
    // If no code at 0, try start at min_addr
    if(pc >= mem.size() || (r32(pc)==0 && min_addr!=UINT32_MAX)) pc = min_addr;

    // Initialize stack pointer near top of memory
    x[2] = (uint32_t)(MEM_SIZE - 64);

    const uint64_t MAX_STEPS = 400000000ull; // generous cap
    for(uint64_t step=0; step<MAX_STEPS; ++step){
        uint32_t inst = r32(pc);
        uint32_t opcode = inst & 0x7F;
        uint32_t rd = (inst >> 7) & 0x1F;
        uint32_t funct3 = (inst >> 12) & 0x7;
        uint32_t rs1 = (inst >> 15) & 0x1F;
        uint32_t rs2 = (inst >> 20) & 0x1F;
        uint32_t funct7 = (inst >> 25) & 0x7F;
        uint32_t imm_i = (uint32_t)sext(inst >> 20, 12);
        uint32_t imm_u = inst & 0xFFFFF000;
        uint32_t imm_s = (uint32_t)sext(((inst >> 7) & 0x1F) | (((inst >> 25) & 0x7F) << 5), 12);
        uint32_t imm_b = (uint32_t)sext((((inst >> 7) & 0x1) << 11) | (((inst >> 8) & 0xF) << 1) | (((inst >> 25) & 0x3F) << 5) | (((inst >> 31) & 0x1) << 12), 13);
        uint32_t imm_j = (uint32_t)sext((((inst >> 21) & 0x3FF) << 1) | (((inst >> 20) & 0x1) << 11) | (((inst >> 12) & 0xFF) << 12) | (((inst >> 31) & 0x1) << 20), 21);

        uint32_t next_pc = pc + 4;
        auto setx = [&](uint32_t r, uint32_t v){ if(r) x[r]=v; };
        bool jump=false;

        switch(opcode){
            case 0x37: // LUI
                setx(rd, imm_u);
                break;
            case 0x17: // AUIPC
                setx(rd, pc + imm_u);
                break;
            case 0x6F: // JAL
                setx(rd, pc + 4);
                next_pc = pc + imm_j;
                jump=true;
                break;
            case 0x67: { // JALR
                uint32_t t = (x[rs1] + (int32_t)imm_i) & ~1u;
                setx(rd, pc + 4);
                next_pc = t;
                jump=true;
                break; }
            case 0x63: { // BRANCH
                int32_t a = (int32_t)x[rs1], b = (int32_t)x[rs2];
                uint32_t ua = x[rs1], ub = x[rs2];
                bool take=false;
                switch(funct3){
                    case 0: take = (ua==ub); break; // BEQ
                    case 1: take = (ua!=ub); break; // BNE
                    case 4: take = (a < b); break;  // BLT
                    case 5: take = (a >= b); break; // BGE
                    case 6: take = (ua < ub); break; // BLTU
                    case 7: take = (ua >= ub); break;// BGEU
                    default: break;
                }
                if(take){ next_pc = pc + imm_b; jump=true; }
                break; }
            case 0x03: { // LOAD
                uint32_t addr = x[rs1] + (int32_t)imm_i;
                // MMIO: 0x30000 status -> 1, 0x30004 data -> 0
                if(addr == 0x30000){ setx(rd, 1); break; }
                if(addr == 0x30004){ setx(rd, 0); break; }
                uint32_t val=0;
                switch(funct3){
                    case 0: val = (uint32_t)(int32_t)(int8_t)r8(addr); break;   // LB
                    case 1: val = (uint32_t)(int32_t)(int16_t)r16(addr); break; // LH
                    case 2: val = r32(addr); break;                              // LW
                    case 4: val = (uint32_t)r8(addr); break;                     // LBU
                    case 5: val = (uint32_t)r16(addr); break;                    // LHU
                    default: break;
                }
                setx(rd, val);
                break; }
            case 0x23: { // STORE
                uint32_t addr = x[rs1] + (int32_t)imm_s;
                // MMIO output: write to 0x30004 prints low 8-bit char
                if(addr == 0x30004){
                    char ch = (char)(x[rs2] & 0xFF);
                    cout << ch;
                    break;
                }
                switch(funct3){
                    case 0: w8(addr, (uint8_t)(x[rs2]&0xFF)); break;  // SB
                    case 1: w16(addr, (uint16_t)(x[rs2]&0xFFFF)); break; // SH
                    case 2: w32(addr, x[rs2]); break;                   // SW
                    default: break;
                }
                break; }
            case 0x13: { // OP-IMM
                uint32_t shamt = (inst >> 20) & 0x1F;
                switch(funct3){
                    case 0: setx(rd, x[rs1] + (int32_t)imm_i); break; // ADDI
                    case 2: setx(rd, (int32_t)x[rs1] < (int32_t)imm_i); break; // SLTI
                    case 3: setx(rd, x[rs1] < imm_i); break; // SLTIU
                    case 4: setx(rd, x[rs1] ^ imm_i); break; // XORI
                    case 6: setx(rd, x[rs1] | imm_i); break; // ORI
                    case 7: setx(rd, x[rs1] & imm_i); break; // ANDI
                    case 1: setx(rd, x[rs1] << shamt); break; // SLLI
                    case 5: {
                        if(((inst >> 30) & 1) == 1) setx(rd, (uint32_t)((int32_t)x[rs1] >> shamt)); // SRAI
                        else setx(rd, x[rs1] >> shamt); // SRLI
                        break; }
                    default: break;
                }
                break; }
            case 0x33: { // OP
                if(funct7 == 0x01){ // M-extension
                    uint32_t a = x[rs1], b = x[rs2];
                    switch(funct3){
                        case 0: { // MUL
                            uint64_t r = (uint64_t)(int64_t)(int32_t)a * (int64_t)(int32_t)b;
                            setx(rd, (uint32_t)r);
                            break; }
                        case 1: { // MULH (signed x signed)
                            int64_t aa = (int64_t)(int32_t)a;
                            int64_t bb = (int64_t)(int32_t)b;
                            unsigned __int128 prod = (unsigned __int128)aa * (unsigned __int128)bb;
                            uint32_t hi = (uint32_t)(prod >> 32);
                            setx(rd, hi);
                            break; }
                        case 2: { // MULHSU (signed x unsigned)
                            int64_t aa = (int64_t)(int32_t)a;
                            uint64_t bb = (uint64_t)b;
                            __int128 prod = (__int128)aa * (__int128)bb;
                            uint32_t hi = (uint32_t)((unsigned __int128)prod >> 32);
                            setx(rd, hi);
                            break; }
                        case 3: { // MULHU (unsigned x unsigned)
                            unsigned __int128 prod = (unsigned __int128)(uint64_t)a * (unsigned __int128)(uint64_t)b;
                            uint32_t hi = (uint32_t)(prod >> 32);
                            setx(rd, hi);
                            break; }
                        case 4: { // DIV
                            int32_t aa = (int32_t)a, bb = (int32_t)b;
                            if(bb == 0) setx(rd, (uint32_t)-1);
                            else if(aa == INT32_MIN && bb == -1) setx(rd, (uint32_t)INT32_MIN);
                            else setx(rd, (uint32_t)(aa / bb));
                            break; }
                        case 5: { // DIVU
                            uint32_t aa = a, bb = b;
                            if(bb == 0) setx(rd, 0xFFFFFFFFu);
                            else setx(rd, (uint32_t)(aa / bb));
                            break; }
                        case 6: { // REM
                            int32_t aa = (int32_t)a, bb = (int32_t)b;
                            if(bb == 0) setx(rd, (uint32_t)aa);
                            else if(aa == INT32_MIN && bb == -1) setx(rd, 0);
                            else setx(rd, (uint32_t)(aa % bb));
                            break; }
                        case 7: { // REMU
                            uint32_t aa = a, bb = b;
                            if(bb == 0) setx(rd, aa);
                            else setx(rd, (uint32_t)(aa % bb));
                            break; }
                        default: break;
                    }
                } else {
                    switch((funct7<<3)|funct3){
                        case (0x00<<3)|0x0: setx(rd, x[rs1] + x[rs2]); break; // ADD
                        case (0x20<<3)|0x0: setx(rd, x[rs1] - x[rs2]); break; // SUB
                        case (0x00<<3)|0x1: setx(rd, x[rs1] << (x[rs2]&0x1F)); break; // SLL
                        case (0x00<<3)|0x2: setx(rd, (int32_t)x[rs1] < (int32_t)x[rs2]); break; // SLT
                        case (0x00<<3)|0x3: setx(rd, x[rs1] < x[rs2]); break; // SLTU
                        case (0x00<<3)|0x4: setx(rd, x[rs1] ^ x[rs2]); break; // XOR
                        case (0x00<<3)|0x5: setx(rd, x[rs1] >> (x[rs2]&0x1F)); break; // SRL
                        case (0x20<<3)|0x5: setx(rd, (uint32_t)((int32_t)x[rs1] >> (x[rs2]&0x1F))); break; // SRA
                        case (0x00<<3)|0x6: setx(rd, x[rs1] | x[rs2]); break; // OR
                        case (0x00<<3)|0x7: setx(rd, x[rs1] & x[rs2]); break; // AND
                        default: break;
                    }
                }
                break; }
            case 0x0F: // FENCE/FENCE.I - no effect
                break;
            case 0x73: { // SYSTEM: ECALL/EBREAK
                uint32_t funct12 = (inst >> 20) & 0xFFF;
                if(funct12==0x000 || funct12==0x001){
                    // Halt execution; output already printed via MMIO if any
                    return 0;
                }
                break; }
            default:
                // Unknown instruction: treat as NOP
                break;
        }

        pc = next_pc;
        x[0]=0;
    }

    // If we reached step cap, output a0 anyway
    cout << (int32_t)x[10] << '\n';
    return 0;
}
