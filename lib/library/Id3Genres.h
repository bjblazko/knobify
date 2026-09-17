#pragma once

#include <cstddef>
#include <string>

namespace knobify::library {

// The numbered genre list ID3v1 defined (0-79) plus the Winamp
// extensions everything since has followed (80-191). Three tag formats
// still refer to genres by number rather than name -- ID3v1's genre byte,
// ID3v2's TCON in its "(17)" form, and MP4's `gnre` atom -- so they all
// resolve through this one table (ADR 0021).
inline const char *id3v1Genre(unsigned index) {
  static const char *const kGenres[] = {
      "Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk", "Grunge",
      "Hip-Hop", "Jazz", "Metal", "New Age", "Oldies", "Other", "Pop", "R&B",
      "Rap", "Reggae", "Rock", "Techno", "Industrial", "Alternative", "Ska",
      "Death Metal", "Pranks", "Soundtrack", "Euro-Techno", "Ambient",
      "Trip-Hop", "Vocal", "Jazz+Funk", "Fusion", "Trance", "Classical",
      "Instrumental", "Acid", "House", "Game", "Sound Clip", "Gospel",
      "Noise", "Alternative Rock", "Bass", "Soul", "Punk", "Space",
      "Meditative", "Instrumental Pop", "Instrumental Rock", "Ethnic",
      "Gothic", "Darkwave", "Techno-Industrial", "Electronic",
      "Pop-Folk", "Eurodance", "Dream", "Southern Rock", "Comedy", "Cult",
      "Gangsta", "Top 40", "Christian Rap", "Pop/Funk", "Jungle",
      "Native American", "Cabaret", "New Wave", "Psychedelic", "Rave",
      "Showtunes", "Trailer", "Lo-Fi", "Tribal", "Acid Punk", "Acid Jazz",
      "Polka", "Retro", "Musical", "Rock & Roll", "Hard Rock", "Folk",
      "Folk-Rock", "National Folk", "Swing", "Fast Fusion", "Bebop",
      "Latin", "Revival", "Celtic", "Bluegrass", "Avantgarde",
      "Gothic Rock", "Progressive Rock", "Psychedelic Rock",
      "Symphonic Rock", "Slow Rock", "Big Band", "Chorus", "Easy Listening",
      "Acoustic", "Humour", "Speech", "Chanson", "Opera", "Chamber Music",
      "Sonata", "Symphony", "Booty Bass", "Primus", "Porn Groove",
      "Satire", "Slow Jam", "Club", "Tango", "Samba", "Folklore",
      "Ballad", "Power Ballad", "Rhythmic Soul", "Freestyle", "Duet",
      "Punk Rock", "Drum Solo", "A Cappella", "Euro-House", "Dance Hall",
      "Goa", "Drum & Bass", "Club-House", "Hardcore", "Terror", "Indie",
      "BritPop", "Negerpunk", "Polsk Punk", "Beat", "Christian Gangsta Rap",
      "Heavy Metal", "Black Metal", "Crossover", "Contemporary Christian",
      "Christian Rock", "Merengue", "Salsa", "Thrash Metal", "Anime",
      "Jpop", "Synthpop", "Abstract", "Art Rock", "Baroque", "Bhangra",
      "Big Beat", "Breakbeat", "Chillout", "Downtempo", "Dub", "EBM",
      "Eclectic", "Electro", "Electroclash", "Emo", "Experimental",
      "Garage", "Global", "IDM", "Illbient", "Industro-Goth", "Jam Band",
      "Krautrock", "Leftfield", "Lounge", "Math Rock", "New Romantic",
      "Nu-Breakz", "Post-Punk", "Post-Rock", "Psytrance", "Shoegaze",
      "Space Rock", "Trop Rock", "World Music", "Neoclassical", "Audiobook",
      "Audio Theatre", "Neue Deutsche Welle", "Podcast", "Indie Rock",
      "G-Funk", "Dubstep", "Garage Rock", "Psybient"};
  static constexpr unsigned kCount = sizeof(kGenres) / sizeof(kGenres[0]);
  return index < kCount ? kGenres[index] : nullptr;
}

// TCON's text carries a genre in any of three shapes: a plain name
// ("Rock"), a bare number ("17"), or the ID3v2.3 reference form
// ("(17)", or "(17)Hard Rock" where the trailing text refines it). The
// refining text wins when present -- it is what the tagger actually
// meant -- and an unknown number resolves to nothing rather than to a
// misleading name.
inline std::string resolveId3GenreText(const std::string &text) {
  if (text.empty()) return "";

  if (text.front() == '(') {
    const size_t close = text.find(')');
    if (close != std::string::npos) {
      const std::string number = text.substr(1, close - 1);
      const std::string rest = text.substr(close + 1);
      if (!rest.empty()) return rest;
      // "(RX)" / "(CR)" are remix/cover markers, not genres.
      if (number.find_first_not_of("0123456789") == std::string::npos &&
          !number.empty()) {
        const char *name = id3v1Genre(
            static_cast<unsigned>(std::stoul(number)));
        return name ? name : "";
      }
      return "";
    }
  }

  if (text.find_first_not_of("0123456789") == std::string::npos) {
    const char *name = id3v1Genre(static_cast<unsigned>(std::stoul(text)));
    return name ? name : "";
  }

  return text;
}

}  // namespace knobify::library
