/*=============================================================================
	Copyright (c) 2012-2013 Richard Otis

	Based on example code from Boost MultiIndex.
	Copyright (c) 2003-2008 Joaquin M Lopez Munoz.
	See http://www.boost.org/libs/multi_index for library home page.

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
=============================================================================*/

// Helper functions for AST-based models

#include "libgibbs/include/libgibbs_pch.hpp"
#include "libgibbs/include/models.hpp"
#include "libgibbs/include/optimizer/opt_Gibbs.hpp"
#include <string>
#include <map>
#include <algorithm>
#include <sstream>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/tuple/tuple.hpp>

using boost::spirit::utree;
typedef boost::spirit::utree_type utree_type;
using boost::multi_index_container;
using namespace boost::multi_index;

sublattice_set build_variable_map(
		const Phase_Collection::const_iterator p_begin,
		const Phase_Collection::const_iterator p_end,
		const evalconditions &conditions,
		std::map<std::string, int> &indices
		) {
	sublattice_set ret_set;

	int indexcount = 0; // counter for variable indices (for optimizer)

	// All phases
	for (auto i = p_begin; i != p_end; ++i) {
		auto const cond_find = conditions.phases.find(i->first);
		if (cond_find->second != PhaseStatus::ENTERED) continue;
		auto subl_start = i->second.get_sublattice_iterator();
		auto subl_end = i->second.get_sublattice_iterator_end();
		std::string phasename = i->first;

		indices[phasename + "_FRAC"] = indexcount; // save index of phase fraction
		// insert fake record for the phase fraction variable at -1 sublattice index

		ret_set.insert(sublattice_entry(-1, indexcount++, 0, phasename, ""));

		// All sublattices
		for (auto j = subl_start; j != subl_end;++j) {
			// All species
			for (auto k = (*j).get_species_iterator(); k != (*j).get_species_iterator_end();++k) {
				// Check if this species in this sublattice is on our list of elements to investigate
				if (std::find(conditions.elements.cbegin(),conditions.elements.cend(),*k) != conditions.elements.cend()) {
					int sublindex = std::distance(subl_start,j);
					double sitecount = (*j).stoi_coef;
					std::string spec = (*k);
					std::stringstream varname;
					varname << phasename << "_" << sublindex << "_" << spec; // build variable name
					indices[varname.str()] = indexcount; // save index of variable

					ret_set.insert(sublattice_entry(sublindex, indexcount++, sitecount, phasename, spec));
				}
			}
		}
	}
	return ret_set;
}

// count the total number of "mixing" sites in a sublattice set
// non-mixing sites are sublattices with only vacancies in them
double count_mixing_sites(const sublattice_set_view &ssv) {
	int curindex = 0;
	int sitecount = 0;
	boost::multi_index::index<sublattice_set_view,myindex>::type::iterator ic0,ic1;
	ic0 = get<myindex>(ssv).lower_bound(curindex);
	ic1 = get<myindex>(ssv).upper_bound(curindex);

	// build subview to only "real" sublattices (exclude fake -1 index)
	while (ic0 != ic1) {
		int speccount = std::distance(ic0,ic1);
		if (!(speccount == 1 && (*ic0)->species == "VA")) {
			// only count non-pure vacancy sites
			sitecount += (*ic0)->num_sites;
		}
		++curindex;
		ic0 = get<myindex>(ssv).lower_bound(curindex);
		ic1 = get<myindex>(ssv).upper_bound(curindex);
	}
	return sitecount;
}

// helper function to add multiplicative factors of (y_i - y_j)**k
utree add_interaction_factor(const std::string &lhs_varname, const std::string &rhs_varname, const double &degree, const utree &input_tree) {
	utree temp_tree, power_tree, ret_tree;
	temp_tree.push_back("-");
	temp_tree.push_back(lhs_varname);
	temp_tree.push_back(rhs_varname);
	power_tree.push_back("**");
	power_tree.push_back(temp_tree);
	power_tree.push_back(degree);
	ret_tree.push_back("*");
	ret_tree.push_back(power_tree);
	ret_tree.push_back(input_tree);
	return ret_tree;
}

// Normalize by the total number of mixing sites
void normalize_utree(utree &input_tree, const sublattice_set_view &ssv) {
	utree temp;
	temp.push_back("/");
	temp.push_back(input_tree);
	temp.push_back(count_mixing_sites(ssv));
	input_tree.swap(temp);
}

utree find_parameter_ast(const sublattice_set_view &subl_view, const parameter_set_view &param_view) {
	std::vector<const Parameter*> matches;
	int sublcount = 0;
	std::vector<std::vector<std::string>> search_config;
	auto subl_start = get<myindex>(subl_view).begin();
	auto subl_end = get<myindex>(subl_view).end();
	// TODO: parameter search would be better with a new index that was a derived key of the sublattice count
	// By the time we get here, we've already been filtered to the correct phase and parameter type
	auto param_iter = get<phase_index>(param_view).begin();
	auto param_end = get<phase_index>(param_view).end();

	// Build search configuration
	while (subl_start != subl_end) {
		int index = (*subl_start)->index;

		if (index < 0) {
			++subl_start;
			continue; // skip "fake" negative indices
		}

		// check if the current index exceeds the known sublattice count
		// expand the size of the vector of vectors by an amount equal to the difference
		// NOTE: if we don't do this, we will crash when we attempt to push_back()
		for (auto i = sublcount; i < (index+1); ++i) {
			std::vector<std::string> tempvec;
			search_config.push_back(tempvec);
			sublcount = index+1;
		}

		// Add species to the parameter search configuration
		search_config[(*subl_start)->index].push_back((*subl_start)->species);

		// Sort the sublattice with the newly added element
		//std::sort(search_config[(*subl_start)->index].begin(), search_config[(*subl_start)->index].end());


		++subl_start;
	}


	// Now that we have a search configuration, search through the parameters in param_view

	while (param_iter != param_end) {
		if (search_config.size() != (*param_iter)->constituent_array.size()) {
			// skip if sublattice counts do not match
			std::cout << "paramskip for sublattice mismatch: " << search_config.size() << " != " << (*param_iter)->constituent_array.size() << std::endl;
			std::cout << "search_config: ";
			for (auto i = search_config.begin(); i != search_config.end(); ++i) {
				for (auto j = (*i).begin(); j != (*i).end(); ++j) {
					std::cout << (*j) << ",";
				}
				std::cout << ":";
			}
			std::cout << std::endl;
			++param_iter;
			continue;
		}
		// We cannot do a direct comparison of the nested vectors because
		//    we must check the case where the wildcard '*' is used in some sublattices in the parameter
		bool isvalid = true;
		auto array_begin = (*param_iter)->constituent_array.begin();
		auto array_iter = array_begin;
		auto array_end = (*param_iter)->constituent_array.end();

		// Now perform a sublattice comparison
		while (array_iter != array_end) {
			const std::string firstelement = *(*array_iter).cbegin();
			// if the parameter sublattices don't match, or the parameter isn't using a wildcard, do not match
			// NOTE: A wildcard will not match if we are looking for a 2+ species interaction in that sublattice
			// The interaction must be explicitly stated to match
			if (!(
				(*array_iter) == search_config[std::distance(array_begin,array_iter)])
				|| (firstelement == "*" && search_config[std::distance(array_begin,array_iter)].size() == 1)
				) {
				isvalid = false;
				break;
			}
			++array_iter;
		}
		if (isvalid) {
			// We found a valid parameter, save it
			matches.push_back(*param_iter);
		}

		++param_iter;
	}

	if (matches.size() >= 1) {
		if (matches.size() == 1) return matches[0]->ast; // exactly one parameter found
		// multiple matching parameters found
		// first, we need to figure out if these are interaction parameters of different polynomial degrees
		// if they are, then all of them are allowed to match
		// if not, match the one with the fewest wildcards
		// TODO: if some have equal numbers of wildcards, choose the first one and warn the user
		std::map<double,const Parameter*> minwilds; // map polynomial degrees to parameters; TODO: this is a dirty hack; I need to add wildcount to Parameter
		bool interactionparam = false;
		bool returnall = true;
		for (auto i = matches.begin(); i != matches.end(); ++i) {
			int wildcount = 0;
			const double curdegree = (*i)->degree;
			const auto array_begin = (*i)->constituent_array.begin();
			const auto array_end = (*i)->constituent_array.end();
			for (auto j = array_begin; j != array_end; ++j) {
				if ((*j)[0] == "*") ++wildcount;
				if ((*j).size() == 2) interactionparam = true; // TODO: only handles binary interactions
			}
			if (minwilds.find(curdegree) == minwilds.end() || wildcount < minwilds[curdegree]->wildcount()) {
				minwilds[curdegree] = (*i);
			}
		}


		// We're fine to return minparam's AST if all polynomial degrees are the same
		// TODO: It seems like it's possible to construct corner cases with duplicate
		// parameters with varying degrees that would confuse this matching.

		if (minwilds.size() == 1) return minwilds.cbegin()->second->ast;

		if (minwilds.size() > 1 && (!interactionparam)) {
			std::cout << "ERROR: Multiple polynomial degrees specified for non-interaction parameters." << std::endl;
			BOOST_THROW_EXCEPTION(internal_error());
		}

		if (minwilds.size() > 1 && interactionparam) {
			utree ret_tree;
			if (minwilds.size() != matches.size()) {
				// not all polynomial degrees here are unique
				// it shouldn't be a problem, it should just mean we matched some based on wildcards
				// (this is just here as a note)
			}
			for (auto param = minwilds.begin(); param != minwilds.end(); ++param) {
				if (param->first == 0) {
					// (y_i - y_j)**0 == 1
					ret_tree = param->second->ast;
					continue;
				}
				// get the names of the variables that are interacting
				std::string lhs_var, rhs_var;
				const auto array_begin = param->second->constituent_array.begin();
				const auto array_end = param->second->constituent_array.end();
				for (auto j = array_begin; j != array_end; ++j) {
					if ((*j).size() == 2) { // TODO: only handles binary interactions
						std::stringstream varname1, varname2;
						varname1 << param->second->phasename() << "_" << std::distance(array_begin,j) << "_" << (*j)[0];
						varname2 << param->second->phasename() << "_" << std::distance(array_begin,j) << "_" << (*j)[1];
						lhs_var = varname1.str();
						rhs_var = varname2.str();
						break; // NOTE: if this parameter has multiple interactions, we don't handle that for now
					}
				}

				// add to the parameter tree a factor of (y_i - y_j)**k, where k is the degree and i,j are interacting
				utree next_term = add_interaction_factor(lhs_var, rhs_var, param->second->degree, param->second->ast);

				// add next_term to the sum
				utree temp_tree;
				temp_tree.push_back("+");
				temp_tree.push_back(ret_tree);
				temp_tree.push_back(next_term);
				ret_tree.swap(temp_tree);
			}

			return ret_tree; // return the parameter tree
		}

		if (minwilds.size() == 0) {
			BOOST_THROW_EXCEPTION(internal_error() << specific_errinfo("Failed to match parameter, but the parameter had already been found"));
		}
	}
	return 0; // no parameter found
}