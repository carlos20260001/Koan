/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Copyright 2002-2011 Haiku, Inc. All rights reserved.
 * Copyright 2025-2026, The Vitruvian Project, All rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _NODE_H
#define _NODE_H

#ifndef __VOS__

#else /* __VOS__ */
#include <OS.h>
#endif /* __VOS__ */
#include <Statable.h>


class BDirectory;
class BEntry;
class BString;
struct entry_ref;

#ifdef __VOS__
namespace BPrivate { class VRefTrackAccess; }

#endif /* __VOS__ */

struct node_ref {
	node_ref();
	node_ref(dev_t device, ino_t node);
#ifdef __VOS__
	//node_ref(vref_id id, uint32 flags);
	node_ref(int fd);
#endif /* __VOS__ */
	node_ref(const node_ref& other);
#ifdef __VOS__
	~node_ref();

	dev_t vdevice() const;
	ino_t vnode() const;

	dev_t device() const;
	ino_t node() const;

	void set_to(dev_t device, ino_t node);

	status_t init_check() const;

	vref_id id() const;
	bool is_virtual() const;
	void unset();
#endif /* __VOS__ */

	bool operator==(const node_ref& other) const;
	bool operator!=(const node_ref& other) const;
	bool operator<(const node_ref& other) const;
	node_ref& operator=(const node_ref& other);

#ifndef __VOS__
	dev_t device;
	ino_t node;
#else /* __VOS__ */
protected:
	dev_t virtual_device;
	ino_t virtual_node;

	dev_t	real_device;
	ino_t	real_node;
	uint64 cache_ticket;

private:
	void _AcquireNodeSlot();
	void _ReleaseNodeSlot();

	team_id team;

	friend struct entry_ref;
	friend class BPrivate::VRefTrackAccess;
#endif /* __VOS__ */
};


class BNode : public BStatable {
public:
								BNode();
								BNode(const entry_ref* ref);
								BNode(const BEntry* entry);
								BNode(const char* path);
								BNode(const BDirectory* dir, const char* path);
								BNode(const BNode& node);
	virtual						~BNode();

			status_t			InitCheck() const;

	virtual	status_t			GetStat(struct stat* st) const;
#ifdef __VOS__
	virtual	status_t			GetStatX(struct statx* stx) const;
#endif /* __VOS__ */

			status_t			SetTo(const entry_ref* ref);
			status_t			SetTo(const BEntry* entry);
			status_t			SetTo(const char* path);
			status_t			SetTo(const BDirectory* dir, const char* path);
			void				Unset();

			status_t			Lock();
			status_t			Unlock();

			status_t			Sync();

			ssize_t				WriteAttr(const char* name, type_code type,
									off_t offset, const void* buffer,
									size_t length);
			ssize_t				ReadAttr(const char* name, type_code type,
									off_t offset, void* buffer,
									size_t length) const;
			status_t			RemoveAttr(const char* name);
			status_t			RenameAttr(const char* oldName,
									const char* newName);
			status_t			GetAttrInfo(const char* name,
									struct attr_info* info) const;
			status_t			GetNextAttrName(char* buffer);
			status_t			RewindAttrs();
			status_t			WriteAttrString(const char* name,
									const BString* data);
			status_t			ReadAttrString(const char* name,
									BString* result) const;

#ifdef __VOS__
			status_t			GetNodeRef(node_ref* ref) const;

#endif /* __VOS__ */
			BNode&				operator=(const BNode& node);
			bool				operator==(const BNode& node) const;
			bool				operator!=(const BNode& node) const;

#ifndef __VOS__
			int					Dup();
				// This should be "const" but R5's is not... Ugggh.
#else /* __VOS__ */
			int					Dup() const;
#endif /* __VOS__ */

private:
	friend class BFile;
	friend class BDirectory;
	friend class BSymLink;

	virtual	void				_RudeNode1();
	virtual	void				_RudeNode2();
	virtual	void				_RudeNode3();
	virtual	void				_RudeNode4();
	virtual	void				_RudeNode5();
	virtual	void				_RudeNode6();

#ifndef __VOS__
private:
#else /* __VOS__ */
protected:
#endif /* __VOS__ */
			status_t			set_fd(int fd);
#ifdef __VOS__

private:
#endif /* __VOS__ */
	virtual	void				close_fd();
			void				set_status(status_t newStatus);

			status_t			_SetTo(int fd, const char* path, bool traverse);
			status_t			_SetTo(const entry_ref* ref, bool traverse);

	virtual	status_t			set_stat(struct stat& stat, uint32 what);

			status_t			_GetStat(struct stat* stat) const;
	virtual	status_t			_GetStat(struct stat_beos* stat) const;
			status_t			InitAttrDir();

private:
			uint32				rudeData[4];
			int					fFd;
#ifndef __VOS__
				// Ffile descriptor for the given node
#else /* __VOS__ */
				// file descriptor for the given node
			node_ref			fNodeRef;
#endif /* __VOS__ */
			int					fAttrFd;
				// file descriptor for the attribute directory of the node,
				// initialized lazily
			status_t			fCStatus;
				// the node's initialization status
};


#endif	// _NODE_H
