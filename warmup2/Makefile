#
# This is the Makefile that can be used to create the "warmup2" executable
# To create "warmup2" executable, do:
#	make warmup2
#
FILES_TO_BACKUP = my402list.c
BACKUP_FNAME = warmup2-A-backup-`date +%d%b%Y-%H%M%S`.tar.gz
BACKUP_DIR = $(HOME)/Shared-ubuntu

warmup2: warmup2.o my402list.o utils.o
	gcc -o warmup2 -g warmup2.o my402list.o utils.o -pthread

warmup2.o: warmup2.c my402list.h utils.h vals.h
	gcc -g -c -Wall warmup2.c

my402list.o: my402list.c my402list.h
	gcc -g -c -Wall my402list.c

utils.o: utils.c utils.h
	gcc -g -c -Wall utils.c

clean:
	rm -f *.o warmup2

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
