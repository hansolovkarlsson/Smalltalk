# Postmortem

*The **scoring**. One entry per mistake or prediction that has met
evidence: one that held, one that failed, or one that held and then
failed, under **Issue**, **Root cause**, **Solution**, **Learnings**. Not
a bug log: a defect belongs here when what it taught outlives it, and a day
that scored nothing adds nothing. The learning is the part that has to be
true a year from now, so it says what would have caught the thing rather
than resolving to be more careful.*

Nothing has been scored here yet. The lessons the first six milestones
produced (the `GC_KIND_OOP_ARRAY` use-after-free, the `strchr` scan past
`'\0'`, the `internIndex()` realloc) are written into the architecture
notes in [`../CLAUDE.md`](../CLAUDE.md), next to the code they explain, and
stay there. Entries here start with the first thing scored after this file
was created.
