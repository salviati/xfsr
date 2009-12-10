xfsr is a set of crude, alpha-quality tools to read files from a damaged XFS partition for GNU/Linux.

### What do you need to rescue a file?

First of all, somewhat healthy superblock fields:
`sb_blocksize sb_agblocks, sb_inodesize, sb_inopblock, sb_blocklog, sb_inodelog,
sb_inopblog, sb_agblklog`.
If you haven't run `mkfs` with any special options, re-running `mkfs` would restore
these values back, BUT be aware that this will format your drive. `xfs_repair`
*might* also help you to restore them.
You need

* A healty inode
* A healty data block.

They usually hold the contents of files, so even if
they're corrupted, you'll still be able to dump your file, but the contents
would be corrupted. If it's a data block of a directory, then bad news, contents
of the directory will be corrupted.
If the superblock is corrupted and you want your disk remain intact, you can
pass these options from commandline.

### How do you rescue a file with these bag of tools?

There's no simple way to locate a file on a partition without a root inode,
so one good idea is to try to climb back at top dir. To do this, one can search
for directories with `xfsr-dirfind` (or `xfsr-rawsearch` along with `xfsr-ls`, if you
can remember the name of the directory you're looking for), list their contents
with `xfsr-ls`, and keep climbing to upper directory. You'll eventually bump into
a dead end since inode of the root directory doesn't exist. This is the furthest
you can go with these tools; run `xfsr-ls` with the inode number/address of
the last "healthy" dir.


### Is it safe to use these tools?

These tools can be considered safe, in the sense that the partiton is opened
read-only, so you don't need to work on a copy of it --as long as you don't
explicitly output the files to your damaged partiton!

The license is GNU GPL v3+, which means there's no guarantee whatsoever. Don't
ever come crying to me if you screw things up.


### Will they work for me?

I hope so. I only tested it on my GNU/Linux machine, and they did their
job well. I haven't heard of any tests under different archs/unices yet.


### Known bugs?

First of, the whole program might be a bug itself. It is dirty, obfuscated to
an extent ---the whole thing was writting in a bad mood: the mood of immense
data loss. And whole thing was not written with release in mind.
I'm publishing the code, because there are no alternatives yet and there
might be people who desperately need such a tool out there. It's not even
alpha quality, but it's better than nothing. I saved more than 99% of my
750GB data with this tool, but again, there might be serious bugs, and
the program *can* fail.

Having said that, I don't know of any bugs, but there are certain things that
were left out, because I though they were of little importance in an average FS:
B+ directories and symlinks with extents are not handled yet.

### Final notes
The include directory was taken directly from xfsprogs-2.9.8 (the version at the
time program was written). You might want to replace it with something better.
And oh, if you're planning to use `xfsr-rawsearch`, check out the `BLOCK_SIZE` define
first. Actually, it might be a good idea to skim the whole code!
