#define pushp(a, b) stp a, b, [sp, #-0x10]!
#define popp(a, b) ldp a, b, [sp], #0x10 

/* `exception_vector.S` send all traps here. */
.global trap_entry
trap_entry:

/* Save registers */
pushp(x29, x30)
pushp(x27, x28)
pushp(x25, x26)
pushp(x23, x24)
pushp(x21, x22)
pushp(x19, x20)
pushp(x17, x18)
pushp(x15, x16)
pushp(x13, x14)
pushp(x11, x12)
pushp(x9, x10)
pushp(x7, x8)
pushp(x5, x6)
pushp(x3, x4)
pushp(x1, x2)

mrs x3, elr_el1
mrs x2, spsr_el1
mrs x1, sp_el0

pushp(x3, x0)
pushp(x1, x2)

/* Call trap(UserContext* context) */
mov x0, sp

bl trap_global_handler

.global trap_ret
trap_ret:

/* Restore registers */
popp(x1, x2)
popp(x3, x0)

msr sp_el0, x1
msr spsr_el1, x2
msr elr_el1, x3

popp(x1, x2)
popp(x3, x4)
popp(x5, x6)
popp(x7, x8)
popp(x9, x10)
popp(x11, x12)
popp(x13, x14)
popp(x15, x16)
popp(x17, x18)
popp(x19, x20)
popp(x21, x22)
popp(x23, x24)
popp(x25, x26)
popp(x27, x28)
popp(x29, x30)

eret

.global trap_return // userret
trap_return:
mov sp, x0
   
b trap_ret