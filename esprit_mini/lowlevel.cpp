static int nesting = 0;

void vPortExitCritical()
{
    nesting--;
    if (!nesting)
    {
        __asm volatile("cpsie i" : : : "memory");
    }
}

void vPortEnterCritical()
{
    __asm volatile("cpsid i" : : : "memory");
    nesting++;
}
