using namespace std;
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "imdb.h"

const char *const imdb::kActorFileName = "actordata";
const char *const imdb::kMovieFileName = "moviedata";

imdb::imdb(const string& directory)
{
  const string actorFileName = directory + "/" + kActorFileName;
  const string movieFileName = directory + "/" + kMovieFileName;
  
  actorFile = acquireFileMap(actorFileName, actorInfo);
  movieFile = acquireFileMap(movieFileName, movieInfo);
}

bool imdb::good() const
{
  return !( (actorInfo.fd == -1) || 
	    (movieInfo.fd == -1) ); 
}

film get_film (const void *file, int offset) {
  
  char *ptr = ((char*)file) + offset;
  film ans;
  ans.title = string(ptr);
  int len = ans.title.size();
  ans.year = 1900 + (int)(*(ptr + len + 1));
  return ans;
}

int compareMovies (const void *first, const void *second) {
  key *cur_key = (key*)first;
  const void *file = cur_key->file;  
  film first_film;
  film sec_film;

  first_film.title = string(cur_key->name);
  first_film.year = cur_key->year;
  sec_film = get_film(file, *((int*)second));

  if (first_film == sec_film) return 0;
  if (first_film < sec_film) return -1;
  return 1;

}

int compareActors(const void *first, const void *second) {

  const void *file = ((key*)first)->file;
  string first_name = ((key*)first)->name;
  int sec_off = *((int*) second);
  char *sec_name = ((char*)file) + sec_off;

  return strcmp(&(first_name[0]), sec_name);
}


bool imdb::getCredits(const string& player, vector<film>& films) const {

  key to_search;
  to_search.name = &(player[0]);
  to_search.file = actorFile;
  void *found_actor = bsearch(&to_search, (int*)actorFile+1, *((int*) actorFile), sizeof(int), compareActors);
  if (found_actor == NULL) return false;

  // actor is already found
  char *actor_ptr = (char*)actorFile + *((int*)found_actor);
  int len = strlen(actor_ptr) + 1;
  if (len % 2) len++;
  char *ptr = actor_ptr + len;
  short movies_num = *((short*)ptr);
  ptr += 2;
  if ((len+2) % 4) ptr += 2;
  int *offsets = (int*)ptr;
  
  for (short i = 0; i < movies_num; i++) {
    films.push_back(get_film(movieFile, offsets[i]));
  }

  return true;
}

bool imdb::getCast(const film& movie, vector<string>& players) const {

  /*
  key *to_search;
  to_search->name = &(movie.title[0]);
  to_search->year = movie.year;
  to_search->file = movieFile;
  */
  key to_search;
  to_search.name = &(movie.title[0]);
  to_search.year = movie.year;
  to_search.file = movieFile;

  void *found_movie = bsearch(&to_search, (int*)movieFile+1, *((int*) movieFile), sizeof(int), compareMovies);
  if (found_movie == NULL) return false;

  // movie is already found
  char *movie_ptr = (char*)movieFile + *((int*)found_movie);
  int len = strlen(movie_ptr) + 2;
  if (len % 2) len++;
  char *ptr = movie_ptr + len;
  short actors_num = *((short*)ptr);
  ptr += 2;
  if ((len+2) % 4) ptr += 2;
  int *offsets = (int*)ptr;

  for (short i = 0; i < actors_num; i++) {
    players.push_back(string((char*)actorFile + offsets[i]));
  }  

  return true;
}

imdb::~imdb()
{
  releaseFileMap(actorInfo);
  releaseFileMap(movieInfo);
}

// ignore everything below... it's all UNIXy stuff in place to make a file look like
// an array of bytes in RAM.. 
const void *imdb::acquireFileMap(const string& fileName, struct fileInfo& info)
{
  struct stat stats;
  stat(fileName.c_str(), &stats);
  info.fileSize = stats.st_size;
  info.fd = open(fileName.c_str(), O_RDONLY);
  return info.fileMap = mmap(0, info.fileSize, PROT_READ, MAP_SHARED, info.fd, 0);
}

void imdb::releaseFileMap(struct fileInfo& info)
{
  if (info.fileMap != NULL) munmap((char *) info.fileMap, info.fileSize);
  if (info.fd != -1) close(info.fd);
}
