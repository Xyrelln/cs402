#
# This is the Makefile that can be used to create the "warmup1" executable
# To create "warmup1" executable, do:
#	make warmup1
#
FILES_TO_BACKUP = my402list.c
BACKUP_FNAME = warmup1-A-backup-`date +%d%b%Y-%H%M%S`.tar.gz
BACKUP_DIR = $(HOME)/Shared-ubuntu

warmup1: warmup1.o my402list.o
	gcc -o warmup1 -g warmup1.o my402list.o

warmup1.o: warmup1.c my402list.h
	gcc -g -c -Wall warmup1.c

my402list.o: my402list.c my402list.h
	gcc -g -c -Wall my402list.c

clean:
	rm -f *.o warmup1

backup:
	# only backup "my402list.c" since this Makefile is for part (A) of the grading guidelines
	# if you want to backup different files, change FILES_TO_BACKUP at the top of this Makefile
	tar cvzf $(BACKUP_FNAME) $(FILES_TO_BACKUP)
	@if [ -d $(BACKUP_DIR)/ ]; then \
		mv $(BACKUP_FNAME) $(BACKUP_DIR)/$(BACKUP_FNAME); \
		echo ; \
		echo "Backup file created in shared folder: $(BACKUP_DIR)/$(BACKUP_FNAME)"; \
		/bin/ls -l $(BACKUP_DIR)/$(BACKUP_FNAME); \
	else \
		echo ; \
		echo "$(BACKUP_DIR) inaccessible, local backup file created: $(BACKUP_FNAME)"; \
		/bin/ls -l $(BACKUP_FNAME); \
	fi
