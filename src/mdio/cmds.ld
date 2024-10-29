SECTIONS
{
    .cmds : {
        PROVIDE(cmds_start = .);
        *(.cmd_registry)
        PROVIDE(cmds_end = .);
    }
}
INSERT AFTER .data;
