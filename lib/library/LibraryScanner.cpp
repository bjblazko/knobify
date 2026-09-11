#include "LibraryScanner.h"

namespace knobify::library {

namespace {

ArtistId findOrAddArtist(LibraryIndex &index, const std::string &name) {
  for (const auto &artist : index.artists) {
    if (artist.name == name) return artist.id;
  }
  ArtistId id = static_cast<ArtistId>(index.artists.size());
  index.artists.push_back(Artist{id, name});
  return id;
}

AlbumId findOrAddAlbum(LibraryIndex &index, ArtistId artistId,
                       const std::string &title) {
  for (const auto &album : index.albums) {
    if (album.artistId == artistId && album.title == title) return album.id;
  }
  AlbumId id = static_cast<AlbumId>(index.albums.size());
  index.albums.push_back(Album{id, artistId, title});
  return id;
}

}  // namespace

LibraryIndex LibraryScanner::scan(FileLister &lister, FileOpener &opener) {
  LibraryIndex index;

  lister.reset();
  FileEntry entry;
  while (lister.next(entry)) {
    auto file = opener.open(entry.path);
    if (!file) {
      continue;  // Unreadable file; skip rather than abort the scan.
    }
    TagResult tags = TagReader::read(*file, entry.path);

    ArtistId artistId = findOrAddArtist(index, tags.artist);
    AlbumId albumId = findOrAddAlbum(index, artistId, tags.album);

    TrackId trackId = static_cast<TrackId>(index.tracks.size());
    index.tracks.push_back(
        Track{trackId, albumId, tags.title, tags.trackNumber, entry.path});
  }

  return index;
}

}  // namespace knobify::library
