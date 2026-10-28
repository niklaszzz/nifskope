#include "nifmodel.h"

#include <QTimer>
#include <QUndoStack>

namespace {
struct UndoItem {
	NifData data;
	QVector<std::shared_ptr<const UndoItem>> children;
};
using ItemPtr = std::shared_ptr<const UndoItem>;

ItemPtr captureItem( NifItem * item, const ItemPtr & previous )
{
	QVector<ItemPtr> children;
	children.reserve( item->childCount() );
	bool same = previous && item->data() == previous->data && item->childCount() == previous->children.size();
	for ( int i = 0; i < item->childCount(); ++i ) {
		ItemPtr old = previous && i < previous->children.size() ? previous->children[i] : ItemPtr();
		auto child = captureItem( item->child(i), old );
		same = same && child == old;
		children << child;
	}
	if ( same ) return previous;
	auto result = std::make_shared<UndoItem>();
	result->data = item->data();
	result->children = std::move( children );
	return result;
}

void restoreChildren( NifItem * parent, const ItemPtr & snapshot )
{
	parent->prepareInsert( snapshot->children.size() );
	for ( const auto & child : snapshot->children ) {
		auto item = parent->insertChild( child->data );
		restoreChildren( item, child );
	}
}
}

struct NifUndoState {
	ItemPtr root;
	quint32 version;
};

class NifSnapshotCommand final : public QUndoCommand {
public:
	NifSnapshotCommand( NifModel * model, std::shared_ptr<const NifUndoState> before,
		std::shared_ptr<const NifUndoState> after, const QString & text )
		: QUndoCommand(text), model(model), before(std::move(before)), after(std::move(after)) {}
	void undo() override { model->restoreUndoState( before ); }
	void redo() override {
		// The edit is already visible when the snapshot is committed.
		if ( first ) first = false;
		else model->restoreUndoState( after );
	}
private:
	NifModel * model;
	std::shared_ptr<const NifUndoState> before, after;
	bool first = true;
};

std::shared_ptr<const NifUndoState> NifModel::captureUndoState() const
{
	auto snapshot = std::make_shared<NifUndoState>();
	snapshot->root = captureItem( root, undoBaseline ? undoBaseline->root : ItemPtr() );
	snapshot->version = version;
	return snapshot;
}

void NifModel::restoreUndoState( const std::shared_ptr<const NifUndoState> & snapshot )
{
	undoRestoring = true;
	emit undoAboutToRestore();
	beginResetModel();
	delete root;
	root = new NifItem( snapshot->root->data, nullptr );
	version = snapshot->version;
	restoreChildren( root, snapshot->root );
	lockUpdates = false;
	needUpdates = utNone;
	resetState();
	invalidateConditions( root );
	updateLinks();
	endResetModel();
	undoBaseline = snapshot;
	undoPending = false;
	undoRestoring = false;
	emit undoRestored();
}

void NifModel::resetUndoHistory()
{
	undoEnabled = false;
	undoPending = false;
	undoDepth = 0;
	undoText.clear();
	undoStack->clear();
	undoBaseline.reset();
	undoBaseline = captureUndoState();
	undoEnabled = true;
}

void NifModel::suspendUndoHistory()
{
	flushUndoHistory();
	undoEnabled = false;
	undoPending = false;
}

void NifModel::queueUndoCapture()
{
	if ( !undoEnabled || undoRestoring || undoPending ) return;
	undoPending = true;
	QTimer::singleShot( 0, this, [this]() { flushUndoHistory(); } );
}

void NifModel::flushUndoHistory()
{
	if ( !undoEnabled || undoRestoring || undoDepth || !undoPending ) return;
	undoPending = false;
	auto after = captureUndoState();
	if ( undoBaseline && (after->root != undoBaseline->root || after->version != undoBaseline->version) )
		undoStack->push( new NifSnapshotCommand( this, undoBaseline, after,
			undoText.isEmpty() ? tr("Edit document") : undoText ) );
	undoBaseline = after;
	undoText.clear();
}

void NifModel::beginUndoTransaction( const QString & text )
{
	if ( !undoDepth ) {
		flushUndoHistory();
		undoText = text;
	}
	++undoDepth;
}

void NifModel::endUndoTransaction()
{
	if ( undoDepth <= 0 ) return;
	if ( --undoDepth == 0 ) {
		// Capture even operations that temporarily suppressed model signals.
		undoPending = true;
		flushUndoHistory();
	}
}

void NifModel::applyCommand( QUndoCommand * command )
{
	// Existing editors still use commands to perform their edit. The document
	// snapshot owns its history, so persistent field indexes never span a reset.
	std::unique_ptr<QUndoCommand> owned( command );
	if ( !undoDepth && undoText.isEmpty() ) undoText = owned->text();
	owned->redo();
	queueUndoCapture();
}

void NifModel::undo() { flushUndoHistory(); if ( !undoDepth ) undoStack->undo(); }
void NifModel::redo() { flushUndoHistory(); if ( !undoDepth ) undoStack->redo(); }
